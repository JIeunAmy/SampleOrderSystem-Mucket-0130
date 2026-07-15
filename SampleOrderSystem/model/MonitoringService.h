#pragma once

// Phase 2 (Model): 모니터링 집계 API - 재고 상태 판정 + 상태별 주문 수 집계
// 참고: CLAUDE.md "모니터링" 메뉴 항목 - 상태별 주문 수 집계(REJECTED 제외), 시료별 재고 현황(여유/부족/고갈)
// 이 헤더는 순수 집계/판정 로직만 제공하며, 콘솔 출력이나 생산 큐 스케줄링에는 관여하지 않는다.

#include <string>
#include <vector>

#include "Order.h"

enum class StockStatus
{
    SURPLUS,
    SHORTAGE,
    DEPLETED
};

// 재고 상태 판정
//  - 고갈(DEPLETED): stock == 0 (demand 값과 무관하게 항상 고갈)
//  - 부족(SHORTAGE): 0 < stock < demand
//  - 여유(SURPLUS):  stock >= demand (demand == 0인 경우도 포함)
inline StockStatus JudgeStockStatus(int stock, int demand)
{
    if (stock == 0)
    {
        return StockStatus::DEPLETED;
    }

    if (stock < demand)
    {
        return StockStatus::SHORTAGE;
    }

    return StockStatus::SURPLUS;
}

// 미출고 주문 수요 합계: 특정 sampleId에 대해 PRODUCING/CONFIRMED 상태 주문의
// 수량(Quantity())을 모두 합산한다. (RESERVED/REJECTED/RELEASE 및 다른 sampleId는 제외)
// RESERVED는 아직 승인되지 않아 확정된 수요가 아니므로 제외한다.
inline int SumUndeliveredDemand(const std::vector<Order>& orders, const std::string& sampleId)
{
    int total = 0;

    for (const auto& order : orders)
    {
        if (order.SampleId() != sampleId)
        {
            continue;
        }

        switch (order.Status())
        {
        case OrderStatus::PRODUCING:
        case OrderStatus::CONFIRMED:
            total += order.Quantity();
            break;
        default:
            break;
        }
    }

    return total;
}

// 상태별 주문 수 집계: REJECTED는 집계에서 제외한다.
struct OrderStatusCounts
{
    int reserved = 0;
    int confirmed = 0;
    int producing = 0;
    int release = 0;
};

inline OrderStatusCounts CountOrdersByStatus(const std::vector<Order>& orders)
{
    OrderStatusCounts counts;

    for (const auto& order : orders)
    {
        switch (order.Status())
        {
        case OrderStatus::RESERVED:
            ++counts.reserved;
            break;
        case OrderStatus::CONFIRMED:
            ++counts.confirmed;
            break;
        case OrderStatus::PRODUCING:
            ++counts.producing;
            break;
        case OrderStatus::RELEASE:
            ++counts.release;
            break;
        default:
            break;
        }
    }

    return counts;
}
