#pragma once

// Phase 4 (Produce): ProductionLine - 생산 라인의 FIFO 대기열, 실 생산량/총 생산 시간 계산,
// wall-clock(실제 경과 시간) 기준 생산 완료 판정을 담당한다.
// 참고: CLAUDE.md "도메인 모델 및 상태 흐름 > 생산 라인(Production Line)",
//       .claude/skills/production/SKILL.md "핵심 원칙: 전역 단일 생산 슬롯"
//
// - 생산 라인은 제품(시료)에 상관없이 전체 시스템에서 단 하나의 활성 생산 job만 가질 수 있다.
//   전역 단일 FIFO 큐(현재 job 1개 + 대기열)로 구성되며, 시료별 독립 큐는 두지 않는다.
// - 실 생산량 = ceil(shortage / yieldRate), 총 생산 시간 = avgProductionTime * 실 생산량(분)
// - 생산 시작/완료 예정 시각은 NowProvider(std::chrono::system_clock 기반)로 얻은 현재 시각을 기준으로
//   기록하며, ProcessCompletions 호출 시점의 "현재 시각"이 예상 완료 시각을 지났으면 완료로 판정한다.
//   따라서 프로그램이 종료되었다가 재실행되어도(ExportState/RestoreState를 통해) 실제 경과 시간만큼
//   생산이 진행된 것으로 취급된다.
// - Sample/Order 클래스 정의, 상태 전이 규칙, 재고 증감 자체는 model 계층(Sample.h/Order.h)의 책임이며
//   이 헤더는 그 API(Order::CompleteProduction 등)를 호출만 한다.
// - JSON 파일 저장/로드는 data_agent가 제공하는 data::ProductionState/SaveProductionState/
//   LoadProductionState를 사용하며, 이 헤더는 그 스키마에 맞는 변환(ExportState/RestoreState)만 담당한다.

#include <chrono>
#include <cmath>
#include <ctime>
#include <deque>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "model/Order.h"
#include "model/Sample.h"
#include "data/Repository.h"

struct ProductionJob
{
    std::string orderId;
    std::string sampleId;
    int shortage = 0;        // 이 job 등록 시점의 부족분
    int actualQuantity = 0;  // ceil(shortage / sample.YieldRate())
    double totalMinutes = 0.0; // sample.AvgProductionTime() * actualQuantity
    std::chrono::system_clock::time_point startedAt;
    std::chrono::system_clock::time_point expectedEndAt; // startedAt + totalMinutes(분)
};

class ProductionLine
{
public:
    using NowProvider = std::function<std::chrono::system_clock::time_point()>;

    explicit ProductionLine(NowProvider nowProvider = [] { return std::chrono::system_clock::now(); })
        : nowProvider_(std::move(nowProvider))
    {
    }

    // 주문을 전역 생산 큐에 등록한다. 현재 진행 중인 job이 없으면(sampleId 무관) 즉시 시작하고,
    // 있으면 새 job의 sampleId가 무엇이든 상관없이 전역 대기열 맨 뒤에 추가한다.
    void Enqueue(const std::string& orderId, const std::string& sampleId, int shortage, const Sample& sample)
    {
        ProductionJob job;
        job.orderId = orderId;
        job.sampleId = sampleId;
        job.shortage = shortage;
        job.actualQuantity = ComputeActualQuantity(shortage, sample.YieldRate());
        job.totalMinutes = sample.AvgProductionTime() * job.actualQuantity;

        if (!currentJob_.has_value())
        {
            job.startedAt = nowProvider_();
        }
        else if (pendingQueue_.empty())
        {
            job.startedAt = currentJob_->expectedEndAt;
        }
        else
        {
            job.startedAt = pendingQueue_.back().expectedEndAt;
        }
        job.expectedEndAt = job.startedAt + MinutesToDuration(job.totalMinutes);

        if (!currentJob_.has_value())
        {
            currentJob_ = job;
        }
        else
        {
            pendingQueue_.push_back(job);
        }
    }

    // 현재 시각 기준으로 완료된 진행 중 job(전역 단일 슬롯)을 순서대로 처리하고,
    // 완료된 orderId 목록을 반환한다. 연쇄적으로 여러 job이 한 번에 완료될 수 있다.
    std::vector<std::string> ProcessCompletions(OrderRepository& orders, SampleRepository& samples)
    {
        std::vector<std::string> completedOrderIds;
        auto now = nowProvider_();

        bool progressed = true;
        while (progressed)
        {
            progressed = false;

            if (!currentJob_.has_value())
            {
                break;
            }

            const ProductionJob& current = currentJob_.value();
            if (now < current.expectedEndAt)
            {
                break;
            }

            Order& order = orders.Find(current.orderId);
            Sample& sample = samples.Find(current.sampleId);
            order.CompleteProduction(sample, current.actualQuantity);
            completedOrderIds.push_back(current.orderId);

            if (!pendingQueue_.empty())
            {
                currentJob_ = pendingQueue_.front();
                pendingQueue_.pop_front();
            }
            else
            {
                currentJob_ = std::nullopt;
            }

            progressed = true;
        }

        return completedOrderIds;
    }

    // 전역적으로 현재 진행 중인 job 조회 (없으면 nullopt)
    std::optional<ProductionJob> CurrentJob() const
    {
        return currentJob_;
    }

    // 전역 대기열 조회 (현재 진행 중인 job은 포함하지 않음, 도착 순서 그대로)
    std::vector<ProductionJob> PendingQueue() const
    {
        return std::vector<ProductionJob>(pendingQueue_.begin(), pendingQueue_.end());
    }

    // 특정 sampleId의 job이 전역 큐(현재 job + 대기열) 어딘가에 이미 존재하는지 확인한다.
    bool HasJobForSample(const std::string& sampleId) const
    {
        if (currentJob_.has_value() && currentJob_->sampleId == sampleId)
        {
            return true;
        }
        for (const auto& job : pendingQueue_)
        {
            if (job.sampleId == sampleId)
            {
                return true;
            }
        }
        return false;
    }

    // 현재 진행 중(current) + 대기(pending) 상태를 모두 data::ProductionState로 변환한다.
    // 리스트의 순서 자체가 FIFO 순서를 나타낸다(첫 번째 항목이 현재 job).
    std::vector<data::ProductionState> ExportState() const
    {
        std::vector<data::ProductionState> result;

        auto appendJob = [&result](const ProductionJob& job)
        {
            data::ProductionState state;
            state.orderId = job.orderId;
            state.productionStartAt = FormatTimePoint(job.startedAt);
            state.productionEndAt = FormatTimePoint(job.expectedEndAt);
            state.actualQuantity = job.actualQuantity;
            result.push_back(state);
        };

        if (currentJob_.has_value())
        {
            appendJob(currentJob_.value());
        }
        for (const auto& job : pendingQueue_)
        {
            appendJob(job);
        }

        return result;
    }

    // states의 각 orderId로 orders에서 sampleId를 조회해 전역 current/pending 대기열을 재구성한다.
    // 리스트의 첫 번째 항목이 current, 이후 항목이 pendingQueue_ 순서다.
    void RestoreState(const std::vector<data::ProductionState>& states, OrderRepository& orders)
    {
        currentJob_ = std::nullopt;
        pendingQueue_.clear();

        for (size_t i = 0; i < states.size(); ++i)
        {
            const auto& state = states[i];
            const Order& order = orders.Find(state.orderId);

            ProductionJob job;
            job.orderId = state.orderId;
            job.sampleId = order.SampleId();
            job.shortage = 0; // 완료 판정에 불필요, 복원 시 알 수 없으므로 0으로 둔다.
            job.actualQuantity = state.actualQuantity;
            job.startedAt = ParseTimePoint(state.productionStartAt);
            job.expectedEndAt = ParseTimePoint(state.productionEndAt);
            job.totalMinutes =
                std::chrono::duration<double, std::ratio<60>>(job.expectedEndAt - job.startedAt).count();

            if (i == 0)
            {
                currentJob_ = job;
            }
            else
            {
                pendingQueue_.push_back(job);
            }
        }
    }

    // ISO 8601 (UTC) <-> time_point 변환
    static std::string FormatTimePoint(std::chrono::system_clock::time_point tp)
    {
        std::time_t timeValue = std::chrono::system_clock::to_time_t(tp);
        std::tm tmValue{};
#if defined(_WIN32)
        gmtime_s(&tmValue, &timeValue);
#else
        gmtime_r(&timeValue, &tmValue);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tmValue, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    static std::chrono::system_clock::time_point ParseTimePoint(const std::string& iso8601)
    {
        std::tm tmValue{};
        std::istringstream iss(iso8601);
        iss >> std::get_time(&tmValue, "%Y-%m-%dT%H:%M:%SZ");

        std::time_t timeValue;
#if defined(_WIN32)
        timeValue = _mkgmtime(&tmValue);
#else
        timeValue = timegm(&tmValue);
#endif
        return std::chrono::system_clock::from_time_t(timeValue);
    }

    // 실 생산량 = ceil(shortage / yieldRate)
    static int ComputeActualQuantity(int shortage, double yieldRate)
    {
        return static_cast<int>(std::ceil(static_cast<double>(shortage) / yieldRate));
    }

    // 소수점 분(double) 단위를 system_clock::duration으로 정확히 변환한다(초 단위 이하까지 반영).
    static std::chrono::system_clock::duration MinutesToDuration(double minutes)
    {
        return std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::duration<double, std::ratio<60>>(minutes));
    }

private:
    NowProvider nowProvider_;
    std::optional<ProductionJob> currentJob_;
    std::deque<ProductionJob> pendingQueue_;
};
