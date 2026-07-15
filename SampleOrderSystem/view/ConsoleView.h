#pragma once

// ConsoleView
//
// View 계층 구현 (Phase 6). 콘솔 입출력만 담당하며,
// 도메인 로직(재고 계산/상태 전이)이나 메뉴 분기 흐름 제어는 포함하지 않는다.
// Controller가 Model에서 조회한 데이터를 인자로 넘겨주면 그 데이터를
// 화면에 렌더링하고, 사용자로부터 받은 raw 입력을 Controller에 반환하는
// 역할만 수행한다.
//
// 상태 값(RESERVED/REJECTED/PRODUCING/CONFIRMED/RELEASE)과 필드명은
// CLAUDE.md의 도메인 용어를 그대로 사용해 출력한다(라벨은 사람이 읽기 좋게 한글로 변환).

#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "model/Order.h"
#include "model/MonitoringService.h"
#include "model/Sample.h"
#include "produce/ProductionLine.h"

class ConsoleView
{
public:
    // Windows 콘솔의 기본 코드페이지(CP949 등)로 인해 UTF-8로 컴파일된 한글
    // 문자열이 깨져 보이는 문제를 막기 위해, 어떤 콘솔 출력이 발생하기 전에
    // 콘솔 입출력 코드페이지를 UTF-8(CP_UTF8)로 전환한다.
    // Windows 외 플랫폼에서는 아무 동작도 하지 않는다.
    ConsoleView()
    {
#if defined(_WIN32)
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
#endif
    }

    // ---------------- 메인 메뉴 ----------------

    void ShowMainMenu()
    {
        std::cout << "\n========== S-Semi 시료 생산주문관리 ==========\n";
        std::cout << "1. 시료 관리\n";
        std::cout << "2. 시료 주문\n";
        std::cout << "3. 주문 승인/거절\n";
        std::cout << "4. 모니터링\n";
        std::cout << "5. 생산 라인\n";
        std::cout << "6. 출고 처리\n";
        std::cout << "0. 종료\n";
        std::cout << "===============================================\n";
    }

    int ReadMenuChoice()
    {
        return ReadInt("메뉴를 선택하세요: ");
    }

    // ---------------- 공통 ----------------

    void ShowMessage(const std::string& message)
    {
        std::cout << message << "\n";
    }

    void PressEnterToContinue()
    {
        std::cout << "계속하려면 Enter 키를 누르세요...";
        std::string dummy;
        std::getline(std::cin, dummy);
    }

    // ---------------- 시료 관리 ----------------

    int ShowSampleManagementMenu()
    {
        std::cout << "\n---- 시료 관리 ----\n";
        std::cout << "1. 시료 등록\n";
        std::cout << "2. 전체 목록 조회\n";
        std::cout << "3. 시료 검색\n";
        std::cout << "0. 이전 메뉴로\n";
        return ReadInt("선택: ");
    }

    std::string ReadSampleId()
    {
        return ReadLine("시료 ID: ");
    }

    std::string ReadSampleName()
    {
        return ReadLine("시료 이름: ");
    }

    double ReadAvgProductionTime()
    {
        return ReadDouble("평균 생산시간(분, 소수점 가능): ");
    }

    double ReadYieldRate()
    {
        return ReadDouble("수율(0.0 ~ 1.0): ");
    }

    int ReadInitialStock()
    {
        std::cout << "초기 재고 수량(미입력 시 0): ";
        std::string line;
        std::getline(std::cin, line);
        if (line.empty())
        {
            return 0;
        }

        try
        {
            return std::stoi(line);
        }
        catch (...)
        {
            ShowMessage("[오류] 숫자로 인식할 수 없어 0으로 처리합니다.");
            return 0;
        }
    }

    void ShowSampleList(const std::vector<Sample>& samples)
    {
        std::cout << "\n---- 시료 목록 ----\n";
        if (samples.empty())
        {
            std::cout << "등록된 시료가 없습니다.\n";
            return;
        }

        PrintSampleTableHeader();
        for (const auto& sample : samples)
        {
            PrintSampleRow(sample);
        }
    }

    std::string ReadSampleSearchKeyword()
    {
        return ReadLine("검색어(이름 등): ");
    }

    void ShowSampleSearchResults(const std::vector<Sample>& results)
    {
        std::cout << "\n---- 검색 결과 ----\n";
        if (results.empty())
        {
            std::cout << "일치하는 시료가 없습니다.\n";
            return;
        }

        PrintSampleTableHeader();
        for (const auto& sample : results)
        {
            PrintSampleRow(sample);
        }
    }

    // ---------------- 시료 주문 ----------------

    std::string ReadCustomerName()
    {
        return ReadLine("고객명: ");
    }

    std::string ReadOrderSampleId()
    {
        return ReadLine("주문할 시료 ID: ");
    }

    int ReadOrderQuantity()
    {
        return ReadInt("주문 수량: ");
    }

    void ShowOrderCreated(const Order& order)
    {
        std::cout << "\n[주문 생성 완료]\n";
        PrintOrderTableHeader();
        PrintOrderRow(order);
    }

    // ---------------- 승인/거절 ----------------

    void ShowReservedOrders(const std::vector<Order*>& orders)
    {
        std::cout << "\n---- 예약(RESERVED) 주문 목록 ----\n";
        if (orders.empty())
        {
            std::cout << "예약된 주문이 없습니다.\n";
            return;
        }

        PrintOrderTableHeader();
        for (const auto* order : orders)
        {
            PrintOrderRow(*order);
        }
    }

    std::string ReadOrderIdToProcess()
    {
        return ReadLine("처리할 주문 ID: ");
    }

    int ReadApproveOrRejectChoice()
    {
        std::cout << "1. 승인   2. 거절\n";
        return ReadInt("선택: ");
    }

    void ShowApprovalResult(const Order& order)
    {
        std::cout << "\n[승인 처리 완료] 주문 " << order.Id()
                   << " -> " << OrderStatusLabel(order.Status()) << "\n";
        PrintOrderTableHeader();
        PrintOrderRow(order);
    }

    void ShowRejectionResult(const Order& order)
    {
        std::cout << "\n[거절 처리 완료] 주문 " << order.Id()
                   << " -> " << OrderStatusLabel(order.Status()) << "\n";
        PrintOrderTableHeader();
        PrintOrderRow(order);
    }

    // ---------------- 모니터링 ----------------

    void ShowOrderStatusCounts(const OrderStatusCounts& counts)
    {
        std::cout << "\n---- 상태별 주문 수 집계 (REJECTED 제외) ----\n";
        std::cout << "예약됨(RESERVED)          : " << counts.reserved << "건\n";
        std::cout << "생산중(PRODUCING)         : " << counts.producing << "건\n";
        std::cout << "출고대기(CONFIRMED)       : " << counts.confirmed << "건\n";
        std::cout << "출고완료(RELEASE)         : " << counts.release << "건\n";
    }

    void ShowStockStatusList(
        const std::vector<std::tuple<Sample, int, StockStatus>>& sampleDemandStatus)
    {
        std::cout << "\n---- 시료별 재고 현황 ----\n";
        if (sampleDemandStatus.empty())
        {
            std::cout << "표시할 시료가 없습니다.\n";
            return;
        }

        std::cout << std::left
                   << std::setw(10) << "시료ID"
                   << std::setw(16) << "이름"
                   << std::setw(8) << "재고"
                   << std::setw(10) << "상태" << "\n";
        std::cout << std::string(44, '-') << "\n";

        for (const auto& [sample, demand, status] : sampleDemandStatus)
        {
            std::cout << std::left
                       << std::setw(10) << sample.Id()
                       << std::setw(16) << sample.Name()
                       << std::setw(8) << sample.Stock()
                       << std::setw(10) << StockStatusLabel(status) << "\n";
        }
    }

    // ---------------- 생산 라인 ----------------

    void ShowProductionLineStatus(
        const std::optional<std::pair<ProductionJob, int>>& currentJob,
        const std::vector<std::pair<ProductionJob, int>>& pendingQueue)
    {
        std::cout << "\n---- 현재 생산 중 ----\n";
        if (!currentJob.has_value())
        {
            std::cout << "현재 생산 중인 job이 없습니다.\n";
        }
        else
        {
            const ProductionJob& job = currentJob->first;
            int orderQuantity = currentJob->second;
            std::cout << "시료 " << job.sampleId
                       << " | 주문 " << job.orderId
                       << " | 실 생산량 " << job.actualQuantity
                       << " | 주문량 " << orderQuantity
                       << " | 총 생산시간 " << job.totalMinutes << "분"
                       << " | 시작 " << FormatTime(job.startedAt)
                       << " | 완료예정 " << FormatTime(job.expectedEndAt) << "\n";
        }

        std::cout << "\n---- FIFO 대기열 ----\n";
        if (pendingQueue.empty())
        {
            std::cout << "대기 중인 주문이 없습니다.\n";
        }
        else
        {
            std::cout << std::left
                       << std::setw(10) << "순번"
                       << std::setw(10) << "시료ID"
                       << std::setw(10) << "주문ID"
                       << std::setw(10) << "실생산량"
                       << std::setw(10) << "주문량" << "\n";
            std::cout << std::string(50, '-') << "\n";
            for (std::size_t i = 0; i < pendingQueue.size(); ++i)
            {
                const ProductionJob& job = pendingQueue[i].first;
                int orderQuantity = pendingQueue[i].second;
                std::cout << std::left
                           << std::setw(10) << (i + 1)
                           << std::setw(10) << job.sampleId
                           << std::setw(10) << job.orderId
                           << std::setw(10) << job.actualQuantity
                           << std::setw(10) << orderQuantity << "\n";
            }
        }
    }

    // ---------------- 출고 처리 ----------------

    void ShowConfirmedOrders(const std::vector<Order*>& orders)
    {
        std::cout << "\n---- 출고 대기(CONFIRMED) 주문 목록 ----\n";
        if (orders.empty())
        {
            std::cout << "출고 대기 중인 주문이 없습니다.\n";
            return;
        }

        PrintOrderTableHeader();
        for (const auto* order : orders)
        {
            PrintOrderRow(*order);
        }
    }

    std::string ReadOrderIdToRelease()
    {
        return ReadLine("출고할 주문 ID: ");
    }

    void ShowReleaseResult(const Order& order)
    {
        std::cout << "\n[출고 처리 완료] 주문 " << order.Id()
                   << " -> " << OrderStatusLabel(order.Status()) << "\n";
        PrintOrderTableHeader();
        PrintOrderRow(order);
    }

private:
    // ---------------- 입력 헬퍼 ----------------

    std::string ReadLine(const std::string& prompt)
    {
        std::cout << prompt;
        std::string line;
        std::getline(std::cin, line);
        return line;
    }

    int ReadInt(const std::string& prompt)
    {
        std::cout << prompt;
        std::string line;
        std::getline(std::cin, line);

        try
        {
            std::size_t pos = 0;
            int value = std::stoi(line, &pos);
            if (pos != line.size())
            {
                throw std::invalid_argument("trailing characters");
            }
            return value;
        }
        catch (...)
        {
            ShowMessage("[오류] 올바른 정수를 입력하세요.");
            return -1;
        }
    }

    double ReadDouble(const std::string& prompt)
    {
        std::cout << prompt;
        std::string line;
        std::getline(std::cin, line);

        try
        {
            std::size_t pos = 0;
            double value = std::stod(line, &pos);
            if (pos != line.size())
            {
                throw std::invalid_argument("trailing characters");
            }
            return value;
        }
        catch (...)
        {
            ShowMessage("[오류] 올바른 실수를 입력하세요.");
            return -1.0;
        }
    }

    // ---------------- 출력 헬퍼 ----------------

    void PrintSampleTableHeader()
    {
        std::cout << std::left
                   << std::setw(10) << "시료ID"
                   << std::setw(16) << "이름"
                   << std::setw(14) << "평균생산시간"
                   << std::setw(8) << "수율"
                   << std::setw(8) << "재고" << "\n";
        std::cout << std::string(56, '-') << "\n";
    }

    void PrintSampleRow(const Sample& sample)
    {
        std::cout << std::left
                   << std::setw(10) << sample.Id()
                   << std::setw(16) << sample.Name()
                   << std::setw(14) << std::fixed << std::setprecision(1) << sample.AvgProductionTime()
                   << std::setw(8) << std::fixed << std::setprecision(2) << sample.YieldRate()
                   << std::setw(8) << sample.Stock() << "\n";
    }

    void PrintOrderTableHeader()
    {
        std::cout << std::left
                   << std::setw(10) << "주문ID"
                   << std::setw(14) << "고객명"
                   << std::setw(10) << "시료ID"
                   << std::setw(8) << "수량"
                   << std::setw(20) << "상태" << "\n";
        std::cout << std::string(62, '-') << "\n";
    }

    void PrintOrderRow(const Order& order)
    {
        std::cout << std::left
                   << std::setw(10) << order.Id()
                   << std::setw(14) << order.CustomerName()
                   << std::setw(10) << order.SampleId()
                   << std::setw(8) << order.Quantity()
                   << std::setw(20) << OrderStatusLabel(order.Status()) << "\n";
    }

    static std::string OrderStatusLabel(OrderStatus status)
    {
        switch (status)
        {
        case OrderStatus::RESERVED:
            return "예약됨(RESERVED)";
        case OrderStatus::REJECTED:
            return "거절됨(REJECTED)";
        case OrderStatus::PRODUCING:
            return "생산중(PRODUCING)";
        case OrderStatus::CONFIRMED:
            return "출고대기(CONFIRMED)";
        case OrderStatus::RELEASE:
            return "출고완료(RELEASE)";
        default:
            return "알수없음";
        }
    }

    static std::string StockStatusLabel(StockStatus status)
    {
        switch (status)
        {
        case StockStatus::SURPLUS:
            return "여유";
        case StockStatus::SHORTAGE:
            return "부족";
        case StockStatus::DEPLETED:
            return "고갈";
        default:
            return "알수없음";
        }
    }

    static std::string FormatTime(std::chrono::system_clock::time_point tp)
    {
        std::time_t timeValue = std::chrono::system_clock::to_time_t(tp);
        std::tm tmValue{};
#if defined(_WIN32)
        localtime_s(&tmValue, &timeValue);
#else
        localtime_r(&timeValue, &tmValue);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tmValue, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
};
