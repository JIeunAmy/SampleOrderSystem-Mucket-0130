#pragma once

// Phase 2 (Model): Sample 클래스 - 시료 ID, 이름, 평균 생산시간, 수율(정상 생산 시료 수 / 총 생산 시료 수), 재고 수량
// 참고: CLAUDE.md "도메인 모델 및 상태 흐름 > 시료(Sample)"
// - 등록된 시료만 주문 가능
// - 실 생산량 = ceil(부족분 / 수율), 총 생산 시간 = 평균 생산시간 * 실 생산량 계산에 필요한 값들을 보유한다
//   (계산 로직 자체는 produce_agent 책임이며, 이 클래스는 값만 제공한다)
// - 재고 증감/조회, 시료 저장소(SampleRepository) 역할을 함께 정의한다

#include <string>
#include <stdexcept>
#include <unordered_map>

class Sample
{
public:
    Sample(std::string id, std::string name, double avgProductionTime, double yieldRate, int initialStock = 0)
        : id_(std::move(id)), name_(std::move(name)), avgProductionTime_(avgProductionTime),
          yieldRate_(yieldRate), stock_(initialStock)
    {
        if (yieldRate_ < 0.0 || yieldRate_ > 1.0)
        {
            throw std::invalid_argument("yieldRate must be within [0, 1]");
        }
        if (initialStock < 0)
        {
            throw std::invalid_argument("initialStock must not be negative");
        }
    }

    const std::string& Id() const { return id_; }
    const std::string& Name() const { return name_; }
    double AvgProductionTime() const { return avgProductionTime_; }
    double YieldRate() const { return yieldRate_; }

    int Stock() const { return stock_; }

    void IncreaseStock(int amount)
    {
        if (amount < 0)
        {
            throw std::invalid_argument("amount must not be negative");
        }
        stock_ += amount;
    }

    void DecreaseStock(int amount)
    {
        if (amount < 0)
        {
            throw std::invalid_argument("amount must not be negative");
        }
        if (stock_ - amount < 0)
        {
            throw std::invalid_argument("stock must not become negative");
        }
        stock_ -= amount;
    }

private:
    std::string id_;
    std::string name_;
    double avgProductionTime_;
    double yieldRate_;
    int stock_;
};

class SampleRepository
{
public:
    void Register(const Sample& sample)
    {
        if (Contains(sample.Id()))
        {
            throw std::invalid_argument("duplicate sample id: " + sample.Id());
        }
        samples_.emplace(sample.Id(), sample);
    }

    bool Contains(const std::string& id) const
    {
        return samples_.find(id) != samples_.end();
    }

    Sample& Find(const std::string& id)
    {
        auto it = samples_.find(id);
        if (it == samples_.end())
        {
            throw std::out_of_range("sample not found: " + id);
        }
        return it->second;
    }

    const Sample& Find(const std::string& id) const
    {
        auto it = samples_.find(id);
        if (it == samples_.end())
        {
            throw std::out_of_range("sample not found: " + id);
        }
        return it->second;
    }

private:
    std::unordered_map<std::string, Sample> samples_;
};
