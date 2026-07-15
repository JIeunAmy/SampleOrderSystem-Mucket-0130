#pragma once

// Phase 5 (Controller): MenuController - 메인 메뉴 분기 처리, View 입력 검증 후 Model/Produce API 호출,
// 결과를 View에 전달하는 흐름 제어.
// 참고: CLAUDE.md "메인 메뉴 구성", "도메인 모델 및 상태 흐름"
//
// Controller는 계산 로직(실 생산량/총 생산 시간/재고 증감)이나 콘솔 출력 문자열을 직접 구현하지 않고,
// model_agent/produce_agent/view_agent가 제공하는 API만 호출한다.

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "model/Sample.h"
#include "model/Order.h"
#include "model/MonitoringService.h"
#include "produce/ProductionLine.h"
#include "data/Repository.h"
#include "view/ConsoleView.h"

class MenuController
{
public:
    MenuController()
    {
        LoadPersistedState();
    }

    void Run()
    {
        bool running = true;
        while (running)
        {
            ProcessProductionCompletions();

            view_.ShowMainMenu();
            int choice = view_.ReadMenuChoice();

            // 표준 입력이 종료(EOF)되었으면 더 이상 진행할 수 없으므로 루프를 종료한다
            // (그렇지 않으면 ReadMenuChoice가 계속 -1을 반환하며 무한 루프에 빠진다).
            if (std::cin.eof())
            {
                break;
            }

            switch (choice)
            {
            case 1:
                HandleSampleManagementMenu();
                break;
            case 2:
                HandleSampleOrder();
                break;
            case 3:
                HandleApprovalOrRejection();
                break;
            case 4:
                HandleMonitoring();
                break;
            case 5:
                HandleProductionLineStatus();
                break;
            case 6:
                HandleRelease();
                break;
            case 0:
                running = false;
                break;
            default:
                view_.ShowMessage("[오류] 올바른 메뉴 번호를 입력하세요.");
                break;
            }
        }
    }

private:
    // ---------------- 초기화 ----------------

    void LoadPersistedState()
    {
        std::vector<Sample> samples = data::LoadSamples();
        for (const auto& sample : samples)
        {
            samples_.Register(sample);
            sampleIds_.push_back(sample.Id());
        }

        std::vector<Order> orders = data::LoadOrders();
        for (const auto& order : orders)
        {
            orders_.Register(order);
            UpdateOrderSequenceFromId(order.Id());
        }

        std::vector<data::ProductionState> states = data::LoadProductionState();
        productionLine_.RestoreState(states, orders_);

        // 복원 직후 이미 만료된 job이 있으면 즉시 완료 반영한다.
        ProcessProductionCompletions();
    }

    // ---------------- 생산 완료 반영 ----------------

    void ProcessProductionCompletions()
    {
        std::vector<std::string> completedOrderIds = productionLine_.ProcessCompletions(orders_, samples_);
        if (completedOrderIds.empty())
        {
            return;
        }

        for (const auto& orderId : completedOrderIds)
        {
            data::SaveOrder(orders_.Find(orderId));
        }
        SaveAllSamples();
        SaveProductionState();
    }

    // ---------------- 시료 관리 ----------------

    void HandleSampleManagementMenu()
    {
        bool back = false;
        while (!back)
        {
            int choice = view_.ShowSampleManagementMenu();
            if (std::cin.eof())
            {
                break;
            }
            switch (choice)
            {
            case 1:
                HandleSampleRegistration();
                break;
            case 2:
                HandleSampleList();
                break;
            case 3:
                HandleSampleSearch();
                break;
            case 0:
                back = true;
                break;
            default:
                view_.ShowMessage("[오류] 올바른 메뉴 번호를 입력하세요.");
                break;
            }
        }
    }

    void HandleSampleRegistration()
    {
        std::string id = view_.ReadSampleId();
        std::string name = view_.ReadSampleName();
        double avgProductionTime = view_.ReadAvgProductionTime();
        double yieldRate = view_.ReadYieldRate();
        int initialStock = view_.ReadInitialStock();

        if (avgProductionTime < 0)
        {
            view_.ShowMessage("[오류] 평균 생산시간을 올바르게 입력하세요.");
            return;
        }
        if (yieldRate < 0.0 || yieldRate > 1.0)
        {
            view_.ShowMessage("[오류] 수율은 0.0 ~ 1.0 사이여야 합니다.");
            return;
        }

        try
        {
            Sample sample(id, name, avgProductionTime, yieldRate, initialStock);
            samples_.Register(sample);
            sampleIds_.push_back(id);
            SaveAllSamples();
            view_.ShowMessage("[등록 완료] 시료 " + id + "가 등록되었습니다.");
        }
        catch (const std::exception& e)
        {
            view_.ShowMessage(std::string("[오류] 시료 등록 실패: ") + e.what());
        }
    }

    void HandleSampleList()
    {
        view_.ShowSampleList(CollectAllSamples());
    }

    void HandleSampleSearch()
    {
        std::string keyword = view_.ReadSampleSearchKeyword();
        std::vector<Sample> allSamples = CollectAllSamples();
        std::vector<Sample> results;
        for (const auto& sample : allSamples)
        {
            if (sample.Name().find(keyword) != std::string::npos ||
                sample.Id().find(keyword) != std::string::npos)
            {
                results.push_back(sample);
            }
        }
        view_.ShowSampleSearchResults(results);
    }

    // ---------------- 시료 주문 ----------------

    void HandleSampleOrder()
    {
        std::string customerName = view_.ReadCustomerName();
        std::string sampleId = view_.ReadOrderSampleId();

        if (!samples_.Contains(sampleId))
        {
            view_.ShowMessage("[오류] 존재하지 않는 시료 ID입니다.");
            return;
        }

        int quantity = view_.ReadOrderQuantity();
        if (quantity <= 0)
        {
            view_.ShowMessage("[오류] 주문 수량은 1 이상이어야 합니다.");
            return;
        }

        std::string orderId = GenerateOrderId();
        Order order(orderId, customerName, sampleId, quantity);
        orders_.Register(order);
        SaveAllOrders();

        view_.ShowOrderCreated(orders_.Find(orderId));
    }

    // ---------------- 승인/거절 ----------------

    void HandleApprovalOrRejection()
    {
        std::vector<Order*> reserved = orders_.FindByStatus(OrderStatus::RESERVED);
        view_.ShowReservedOrders(reserved);
        if (reserved.empty())
        {
            return;
        }

        std::string orderId = view_.ReadOrderIdToProcess();
        if (!orders_.Contains(orderId))
        {
            view_.ShowMessage("[오류] 존재하지 않는 주문 ID입니다.");
            return;
        }

        Order& order = orders_.Find(orderId);
        if (order.Status() != OrderStatus::RESERVED)
        {
            view_.ShowMessage("[오류] 예약(RESERVED) 상태의 주문만 승인/거절할 수 있습니다.");
            return;
        }

        int choice = view_.ReadApproveOrRejectChoice();
        if (choice == 1)
        {
            const Sample& sample = samples_.Find(order.SampleId());

            bool hasActiveJob = productionLine_.CurrentJob(order.SampleId()).has_value();
            int availableStock;
            if (hasActiveJob)
            {
                availableStock = 0;
            }
            else
            {
                std::vector<Order> allOrders = CollectAllOrders();
                int confirmedQty = SumConfirmedQuantity(allOrders, order.SampleId());
                availableStock = sample.Stock() - confirmedQty;
            }
            int shortage = order.Quantity() - availableStock;

            order.Approve(availableStock);
            if (order.Status() == OrderStatus::PRODUCING)
            {
                productionLine_.Enqueue(order.Id(), order.SampleId(), shortage, samples_.Find(order.SampleId()));
                SaveProductionState();
            }

            data::SaveOrder(order);
            view_.ShowApprovalResult(order);
        }
        else if (choice == 2)
        {
            order.Reject();
            data::SaveOrder(order);
            view_.ShowRejectionResult(order);
        }
        else
        {
            view_.ShowMessage("[오류] 1(승인) 또는 2(거절)를 입력하세요.");
        }
    }

    // ---------------- 모니터링 ----------------

    void HandleMonitoring()
    {
        std::vector<Order> allOrders = CollectAllOrders();

        OrderStatusCounts counts = CountOrdersByStatus(allOrders);
        view_.ShowOrderStatusCounts(counts);

        std::vector<std::tuple<Sample, int, StockStatus>> sampleDemandStatus;
        for (const auto& sampleId : sampleIds_)
        {
            const Sample& sample = samples_.Find(sampleId);
            int demand = SumUndeliveredDemand(allOrders, sampleId);
            StockStatus status = JudgeStockStatus(sample.Stock(), demand);
            sampleDemandStatus.emplace_back(sample, demand, status);
        }
        view_.ShowStockStatusList(sampleDemandStatus);
    }

    // ---------------- 생산 라인 ----------------

    void HandleProductionLineStatus()
    {
        std::vector<std::pair<std::string, ProductionJob>> currentJobs;
        std::vector<std::pair<std::string, std::vector<ProductionJob>>> pendingQueues;

        for (const auto& sampleId : sampleIds_)
        {
            auto current = productionLine_.CurrentJob(sampleId);
            if (current.has_value())
            {
                currentJobs.emplace_back(sampleId, current.value());
            }

            std::vector<ProductionJob> pending = productionLine_.PendingQueue(sampleId);
            if (!pending.empty())
            {
                pendingQueues.emplace_back(sampleId, pending);
            }
        }

        view_.ShowProductionLineStatus(currentJobs, pendingQueues);
    }

    // ---------------- 출고 처리 ----------------

    void HandleRelease()
    {
        std::vector<Order*> confirmed = orders_.FindByStatus(OrderStatus::CONFIRMED);
        view_.ShowConfirmedOrders(confirmed);
        if (confirmed.empty())
        {
            return;
        }

        std::string orderId = view_.ReadOrderIdToRelease();
        if (!orders_.Contains(orderId))
        {
            view_.ShowMessage("[오류] 존재하지 않는 주문 ID입니다.");
            return;
        }

        Order& order = orders_.Find(orderId);
        if (order.Status() != OrderStatus::CONFIRMED)
        {
            view_.ShowMessage("[오류] 출고대기(CONFIRMED) 상태의 주문만 출고 처리할 수 있습니다.");
            return;
        }

        try
        {
            Sample& sample = samples_.Find(order.SampleId());
            order.Release(sample);
        }
        catch (const std::exception& e)
        {
            view_.ShowMessage(std::string("[오류] 출고 처리 실패: ") + e.what());
            return;
        }

        data::SaveOrder(order);
        SaveAllSamples();
        view_.ShowReleaseResult(order);
    }

    // ---------------- 헬퍼 ----------------

    std::vector<Sample> CollectAllSamples() const
    {
        std::vector<Sample> result;
        result.reserve(sampleIds_.size());
        for (const auto& id : sampleIds_)
        {
            result.push_back(samples_.Find(id));
        }
        return result;
    }

    std::vector<Order> CollectAllOrders()
    {
        std::vector<Order> result;
        std::vector<Order*> all = orders_.All();
        result.reserve(all.size());
        for (const auto* order : all)
        {
            result.push_back(*order);
        }
        return result;
    }

    void SaveAllSamples()
    {
        data::SaveSamples(CollectAllSamples());
    }

    void SaveAllOrders()
    {
        data::SaveOrders(CollectAllOrders());
    }

    void SaveProductionState()
    {
        data::SaveProductionState(productionLine_.ExportState());
    }

    std::string GenerateOrderId()
    {
        ++orderSequence_;
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "ORD-%04d", orderSequence_);
        return std::string(buffer);
    }

    // 저장된 주문 ID("ORD-0001" 형식)를 읽어 재실행 시에도 ID가 중복되지 않도록
    // 시퀀스 카운터를 그 이상으로 맞춘다.
    void UpdateOrderSequenceFromId(const std::string& orderId)
    {
        const std::string prefix = "ORD-";
        if (orderId.size() <= prefix.size() || orderId.compare(0, prefix.size(), prefix) != 0)
        {
            return;
        }

        try
        {
            int numericPart = std::stoi(orderId.substr(prefix.size()));
            if (numericPart > orderSequence_)
            {
                orderSequence_ = numericPart;
            }
        }
        catch (...)
        {
            // 형식이 다른 주문 ID는 무시한다.
        }
    }

    SampleRepository samples_;
    OrderRepository orders_;
    ProductionLine productionLine_;
    ConsoleView view_;

    std::vector<std::string> sampleIds_;
    int orderSequence_ = 0;
};
