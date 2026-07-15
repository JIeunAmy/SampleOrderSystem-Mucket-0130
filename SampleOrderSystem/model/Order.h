#pragma once

// Phase 2 (Model): Order 클래스 - 주문번호, 시료 ID, 고객명, 주문 수량, 상태(RESERVED/REJECTED/
// PRODUCING/CONFIRMED/RELEASE), 상태 전이 메서드
// 참고: CLAUDE.md "도메인 모델 및 상태 흐름 > 주문(Order)"
// 상태 전이: RESERVED -> (승인) -> 재고 충분 시 CONFIRMED, 재고 부족 시 PRODUCING
//           RESERVED -> (거절) -> REJECTED
//           PRODUCING -> (생산 완료) -> CONFIRMED (produce_agent가 계산한 실 생산량만큼 Sample 재고 증가)
//           CONFIRMED -> (출고 처리) -> RELEASE
// FIFO 큐 스케줄링, 실 생산량/총 생산 시간 계산, wall-clock 완료 판정은 produce_agent 책임이며
// 이 클래스는 상태 전이와 재고 반영 API만 제공한다.

#include <string>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "Sample.h"

enum class OrderStatus
{
    RESERVED,
    REJECTED,
    PRODUCING,
    CONFIRMED,
    RELEASE
};

class Order
{
public:
    Order(std::string orderId, std::string customerName, std::string sampleId, int quantity)
        : orderId_(std::move(orderId)), customerName_(std::move(customerName)),
          sampleId_(std::move(sampleId)), quantity_(quantity), status_(OrderStatus::RESERVED)
    {
    }

    const std::string& Id() const { return orderId_; }
    const std::string& CustomerName() const { return customerName_; }
    const std::string& SampleId() const { return sampleId_; }
    int Quantity() const { return quantity_; }
    OrderStatus Status() const { return status_; }

    void Approve(int availableStock)
    {
        if (status_ != OrderStatus::RESERVED)
        {
            throw std::logic_error("Order can only be approved from RESERVED status");
        }

        status_ = (availableStock >= quantity_) ? OrderStatus::CONFIRMED : OrderStatus::PRODUCING;
    }

    void Reject()
    {
        if (status_ != OrderStatus::RESERVED)
        {
            throw std::logic_error("Order can only be rejected from RESERVED status");
        }

        status_ = OrderStatus::REJECTED;
    }

    void CompleteProduction(Sample& sample, int producedQuantity)
    {
        if (status_ != OrderStatus::PRODUCING)
        {
            throw std::logic_error("Order can only complete production from PRODUCING status");
        }

        sample.IncreaseStock(producedQuantity);
        status_ = OrderStatus::CONFIRMED;
    }

    void Release(Sample& sample)
    {
        if (status_ != OrderStatus::CONFIRMED)
        {
            throw std::logic_error("Order can only be released from CONFIRMED status");
        }

        sample.DecreaseStock(quantity_);
        status_ = OrderStatus::RELEASE;
    }

private:
    std::string orderId_;
    std::string customerName_;
    std::string sampleId_;
    int quantity_;
    OrderStatus status_;
};

// Phase 2 (Model): Order에 대한 순수 인메모리 저장소.
// data_agent가 이 API 위에 JSON 영속성 계층을, produce_agent가 FIFO 큐 구성 시 참조할 수 있도록
// 등록/조회/검색 API만 제공한다(콘솔 출력, 메뉴 분기, 생산 스케줄링에는 관여하지 않음).
class OrderRepository
{
public:
    void Register(const Order& order)
    {
        if (Contains(order.Id()))
        {
            throw std::invalid_argument("duplicate order id: " + order.Id());
        }
        orders_.emplace(order.Id(), order);
    }

    bool Contains(const std::string& orderId) const
    {
        return orders_.find(orderId) != orders_.end();
    }

    Order& Find(const std::string& orderId)
    {
        auto it = orders_.find(orderId);
        if (it == orders_.end())
        {
            throw std::out_of_range("order not found: " + orderId);
        }
        return it->second;
    }

    const Order& Find(const std::string& orderId) const
    {
        auto it = orders_.find(orderId);
        if (it == orders_.end())
        {
            throw std::out_of_range("order not found: " + orderId);
        }
        return it->second;
    }

    std::vector<Order*> FindBySampleId(const std::string& sampleId)
    {
        std::vector<Order*> result;
        for (auto& [id, order] : orders_)
        {
            if (order.SampleId() == sampleId)
            {
                result.push_back(&order);
            }
        }
        return result;
    }

    std::vector<Order*> FindByStatus(OrderStatus status)
    {
        std::vector<Order*> result;
        for (auto& [id, order] : orders_)
        {
            if (order.Status() == status)
            {
                result.push_back(&order);
            }
        }
        return result;
    }

    std::vector<Order*> All()
    {
        std::vector<Order*> result;
        result.reserve(orders_.size());
        for (auto& [id, order] : orders_)
        {
            result.push_back(&order);
        }
        return result;
    }

private:
    std::unordered_map<std::string, Order> orders_;
};
