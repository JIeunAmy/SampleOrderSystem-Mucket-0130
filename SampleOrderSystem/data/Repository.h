#pragma once

// data_agent 담당 영역: 시료/주문/생산 진행 상태의 JSON 파일 영속성 계층.
//
// 이 헤더는 Phase 3 구현체이다. 외부 JSON 라이브러리를 도입하지 않고, 이 프로젝트에서 실제로
// 필요한 "중첩 없는 객체 배열" 스키마만을 대상으로 하는 경량 파서/라이터를 직접 구현한다.
//
// 저장 스키마:
// - samples.json          : [{ "sampleId", "name", "avgProductionTime", "yieldRate", "stock" }, ...]
// - orders.json            : [{ "orderId", "customerName", "sampleId", "quantity", "status" }, ...]
// - production_state.json  : [{ "orderId", "productionStartAt", "productionEndAt", "actualQuantity", "shortage" }, ...]
//
// 주의:
// - 상태 전이 규칙, 실 생산량/총 생산 시간 계산, wall-clock 완료 판정 등 도메인 로직은
//   여기서 구현하지 않는다 (각각 model_agent, produce_agent 책임).
// - 콘솔 입출력/메뉴 처리는 여기서 다루지 않는다 (view_agent/controller_agent 책임).
// - Order는 상태 전이 가드가 있는 클래스이므로(model/Order.h 수정 불가), 저장된 상태를 그대로
//   복원하기 위해 Order가 이미 공개한 전이 메서드(Approve/Reject/Release)를 재사용해 목표 상태에
//   도달시킨다. 이는 도메인 로직을 다시 구현하는 것이 아니라 기존 공개 API로 상태를 "재생"하는
//   직렬화 복원 절차일 뿐이다.

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "model/Order.h"
#include "model/Sample.h"

namespace data
{
    constexpr const char* kSamplesFilePath = "samples.json";
    constexpr const char* kOrdersFilePath = "orders.json";
    constexpr const char* kProductionStateFilePath = "production_state.json";

    struct ProductionState
    {
        std::string orderId;
        std::string productionStartAt;  // ISO 8601
        std::string productionEndAt;    // ISO 8601
        int actualQuantity = 0;
        int shortage = 0;
    };

    namespace detail
    {
        // ---- 최소 JSON 파서 (중첩 없는 객체 배열 전용) ----

        using JsonObject = std::unordered_map<std::string, std::string>;

        inline void SkipWhitespace(const std::string& text, size_t& pos)
        {
            while (pos < text.size() &&
                   (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r'))
            {
                ++pos;
            }
        }

        // pos는 여는 큰따옴표를 가리켜야 한다. 파싱 후 pos는 닫는 큰따옴표 다음을 가리킨다.
        inline std::string ParseJsonString(const std::string& text, size_t& pos)
        {
            if (pos >= text.size() || text[pos] != '"')
            {
                throw std::runtime_error("expected '\"' while parsing JSON string");
            }
            ++pos; // opening quote

            std::string result;
            while (pos < text.size() && text[pos] != '"')
            {
                char c = text[pos];
                if (c == '\\' && pos + 1 < text.size())
                {
                    char next = text[pos + 1];
                    switch (next)
                    {
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    case 'r': result += '\r'; break;
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    default: result += next; break;
                    }
                    pos += 2;
                }
                else
                {
                    result += c;
                    ++pos;
                }
            }

            if (pos >= text.size())
            {
                throw std::runtime_error("unterminated JSON string");
            }
            ++pos; // closing quote
            return result;
        }

        // 문자열이 아닌 값(숫자/불리언 등)을 ',', '}', ']' 또는 공백 이전까지 원문 그대로 읽는다.
        inline std::string ParseJsonRawToken(const std::string& text, size_t& pos)
        {
            size_t start = pos;
            while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']' &&
                   text[pos] != ' ' && text[pos] != '\t' && text[pos] != '\n' && text[pos] != '\r')
            {
                ++pos;
            }
            return text.substr(start, pos - start);
        }

        inline JsonObject ParseJsonObject(const std::string& text, size_t& pos)
        {
            JsonObject obj;
            SkipWhitespace(text, pos);
            if (pos >= text.size() || text[pos] != '{')
            {
                throw std::runtime_error("expected '{' while parsing JSON object");
            }
            ++pos;
            SkipWhitespace(text, pos);

            if (pos < text.size() && text[pos] == '}')
            {
                ++pos;
                return obj;
            }

            while (true)
            {
                SkipWhitespace(text, pos);
                std::string key = ParseJsonString(text, pos);
                SkipWhitespace(text, pos);
                if (pos >= text.size() || text[pos] != ':')
                {
                    throw std::runtime_error("expected ':' while parsing JSON object");
                }
                ++pos;
                SkipWhitespace(text, pos);

                std::string value;
                if (pos < text.size() && text[pos] == '"')
                {
                    value = ParseJsonString(text, pos);
                }
                else
                {
                    value = ParseJsonRawToken(text, pos);
                }
                obj[key] = value;

                SkipWhitespace(text, pos);
                if (pos >= text.size())
                {
                    throw std::runtime_error("unterminated JSON object");
                }
                if (text[pos] == ',')
                {
                    ++pos;
                    continue;
                }
                if (text[pos] == '}')
                {
                    ++pos;
                    break;
                }
                throw std::runtime_error("expected ',' or '}' while parsing JSON object");
            }

            return obj;
        }

        inline std::vector<JsonObject> ParseJsonObjectArray(const std::string& text)
        {
            std::vector<JsonObject> result;
            size_t pos = 0;
            SkipWhitespace(text, pos);
            if (pos >= text.size() || text[pos] != '[')
            {
                throw std::runtime_error("expected '[' at top level");
            }
            ++pos;
            SkipWhitespace(text, pos);

            if (pos < text.size() && text[pos] == ']')
            {
                return result;
            }

            while (true)
            {
                SkipWhitespace(text, pos);
                result.push_back(ParseJsonObject(text, pos));
                SkipWhitespace(text, pos);
                if (pos >= text.size())
                {
                    throw std::runtime_error("unterminated JSON array");
                }
                if (text[pos] == ',')
                {
                    ++pos;
                    continue;
                }
                if (text[pos] == ']')
                {
                    ++pos;
                    break;
                }
                throw std::runtime_error("expected ',' or ']' while parsing JSON array");
            }

            return result;
        }

        inline std::string ReadFileToString(const std::string& filePath)
        {
            std::ifstream ifs(filePath, std::ios::binary);
            if (!ifs.is_open())
            {
                throw std::runtime_error("cannot open file: " + filePath);
            }
            std::ostringstream oss;
            oss << ifs.rdbuf();
            return oss.str();
        }

        // ---- JSON 값 이스케이프/직렬화 헬퍼 ----

        inline std::string EscapeJsonString(const std::string& value)
        {
            std::string result;
            result.reserve(value.size());
            for (char c : value)
            {
                switch (c)
                {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\t': result += "\\t"; break;
                case '\r': result += "\\r"; break;
                default: result += c; break;
                }
            }
            return result;
        }

        inline std::string ToJsonString(const std::string& value)
        {
            return "\"" + EscapeJsonString(value) + "\"";
        }

        inline std::string OrderStatusToString(OrderStatus status)
        {
            switch (status)
            {
            case OrderStatus::RESERVED: return "RESERVED";
            case OrderStatus::REJECTED: return "REJECTED";
            case OrderStatus::PRODUCING: return "PRODUCING";
            case OrderStatus::CONFIRMED: return "CONFIRMED";
            case OrderStatus::RELEASE: return "RELEASE";
            }
            return "RESERVED";
        }

        inline OrderStatus OrderStatusFromString(const std::string& text)
        {
            if (text == "RESERVED") return OrderStatus::RESERVED;
            if (text == "REJECTED") return OrderStatus::REJECTED;
            if (text == "PRODUCING") return OrderStatus::PRODUCING;
            if (text == "CONFIRMED") return OrderStatus::CONFIRMED;
            if (text == "RELEASE") return OrderStatus::RELEASE;
            throw std::runtime_error("unknown OrderStatus: " + text);
        }

        // Order는 상태 전이 가드를 가진 클래스라 임의로 status_를 대입할 수 없다.
        // 이미 공개된 전이 메서드(Approve/Reject/Release)를 재사용해 목표 상태로 "재생"한다.
        inline Order ReconstructOrder(const std::string& orderId, const std::string& customerName,
                                       const std::string& sampleId, int quantity, OrderStatus targetStatus)
        {
            Order order(orderId, customerName, sampleId, quantity);

            switch (targetStatus)
            {
            case OrderStatus::RESERVED:
                break;
            case OrderStatus::REJECTED:
                order.Reject();
                break;
            case OrderStatus::CONFIRMED:
            {
                Sample sufficientStock("__reconstruct__", "__reconstruct__", 1, 1.0, quantity);
                order.Approve(sufficientStock.Stock()); // 재고 충분 -> CONFIRMED
                break;
            }
            case OrderStatus::PRODUCING:
            {
                Sample noStock("__reconstruct__", "__reconstruct__", 1, 1.0, 0);
                order.Approve(noStock.Stock()); // 재고 부족 -> PRODUCING (quantity > 0 가정)
                break;
            }
            case OrderStatus::RELEASE:
            {
                Sample sufficientStock("__reconstruct__", "__reconstruct__", 1, 1.0, quantity);
                order.Approve(sufficientStock.Stock()); // -> CONFIRMED
                order.Release(sufficientStock); // -> RELEASE
                break;
            }
            }

            return order;
        }

        inline void WriteFile(const std::string& filePath, const std::string& content)
        {
            std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
            ofs << content;
        }
    } // namespace detail

    // ---- Sample ----

    inline void SaveSamples(const std::vector<Sample>& samples, const std::string& filePath = kSamplesFilePath)
    {
        std::ostringstream oss;
        oss << "[\n";
        for (size_t i = 0; i < samples.size(); ++i)
        {
            const Sample& s = samples[i];
            oss << "  {\"sampleId\":" << detail::ToJsonString(s.Id())
                << ",\"name\":" << detail::ToJsonString(s.Name())
                << ",\"avgProductionTime\":" << s.AvgProductionTime()
                << ",\"yieldRate\":" << s.YieldRate()
                << ",\"stock\":" << s.Stock() << "}";
            if (i + 1 < samples.size())
            {
                oss << ",";
            }
            oss << "\n";
        }
        oss << "]";
        detail::WriteFile(filePath, oss.str());
    }

    inline std::vector<Sample> LoadSamples(const std::string& filePath = kSamplesFilePath)
    {
        std::vector<Sample> result;
        try
        {
            std::string text = detail::ReadFileToString(filePath);
            auto objects = detail::ParseJsonObjectArray(text);
            result.reserve(objects.size());
            for (const auto& obj : objects)
            {
                std::string id = obj.count("sampleId") ? obj.at("sampleId") : "";
                std::string name = obj.count("name") ? obj.at("name") : "";
                double avgProductionTime = obj.count("avgProductionTime") ? std::stod(obj.at("avgProductionTime")) : 0.0;
                double yieldRate = obj.count("yieldRate") ? std::stod(obj.at("yieldRate")) : 0.0;
                int stock = obj.count("stock") ? std::stoi(obj.at("stock")) : 0;
                result.emplace_back(id, name, avgProductionTime, yieldRate, stock);
            }
        }
        catch (...)
        {
            return {};
        }
        return result;
    }

    // ---- Order ----

    inline void SaveOrders(const std::vector<Order>& orders, const std::string& filePath = kOrdersFilePath)
    {
        std::ostringstream oss;
        oss << "[\n";
        for (size_t i = 0; i < orders.size(); ++i)
        {
            const Order& o = orders[i];
            oss << "  {\"orderId\":" << detail::ToJsonString(o.Id())
                << ",\"customerName\":" << detail::ToJsonString(o.CustomerName())
                << ",\"sampleId\":" << detail::ToJsonString(o.SampleId())
                << ",\"quantity\":" << o.Quantity()
                << ",\"status\":" << detail::ToJsonString(detail::OrderStatusToString(o.Status())) << "}";
            if (i + 1 < orders.size())
            {
                oss << ",";
            }
            oss << "\n";
        }
        oss << "]";
        detail::WriteFile(filePath, oss.str());
    }

    inline std::vector<Order> LoadOrders(const std::string& filePath = kOrdersFilePath)
    {
        std::vector<Order> result;
        try
        {
            std::string text = detail::ReadFileToString(filePath);
            auto objects = detail::ParseJsonObjectArray(text);
            result.reserve(objects.size());
            for (const auto& obj : objects)
            {
                std::string orderId = obj.count("orderId") ? obj.at("orderId") : "";
                std::string customerName = obj.count("customerName") ? obj.at("customerName") : "";
                std::string sampleId = obj.count("sampleId") ? obj.at("sampleId") : "";
                int quantity = obj.count("quantity") ? std::stoi(obj.at("quantity")) : 0;
                OrderStatus status = obj.count("status")
                    ? detail::OrderStatusFromString(obj.at("status"))
                    : OrderStatus::RESERVED;

                result.push_back(detail::ReconstructOrder(orderId, customerName, sampleId, quantity, status));
            }
        }
        catch (...)
        {
            return {};
        }
        return result;
    }

    inline void SaveOrder(const Order& order, const std::string& filePath = kOrdersFilePath)
    {
        std::vector<Order> orders = LoadOrders(filePath);

        bool found = false;
        for (size_t i = 0; i < orders.size(); ++i)
        {
            if (orders[i].Id() == order.Id())
            {
                orders[i] = detail::ReconstructOrder(order.Id(), order.CustomerName(), order.SampleId(),
                                                      order.Quantity(), order.Status());
                found = true;
                break;
            }
        }

        if (!found)
        {
            orders.push_back(order);
        }

        SaveOrders(orders, filePath);
    }

    // ---- ProductionState ----

    inline void SaveProductionState(const std::vector<ProductionState>& states,
                                     const std::string& filePath = kProductionStateFilePath)
    {
        std::ostringstream oss;
        oss << "[\n";
        for (size_t i = 0; i < states.size(); ++i)
        {
            const ProductionState& s = states[i];
            oss << "  {\"orderId\":" << detail::ToJsonString(s.orderId)
                << ",\"productionStartAt\":" << detail::ToJsonString(s.productionStartAt)
                << ",\"productionEndAt\":" << detail::ToJsonString(s.productionEndAt)
                << ",\"actualQuantity\":" << s.actualQuantity
                << ",\"shortage\":" << s.shortage << "}";
            if (i + 1 < states.size())
            {
                oss << ",";
            }
            oss << "\n";
        }
        oss << "]";
        detail::WriteFile(filePath, oss.str());
    }

    inline std::vector<ProductionState> LoadProductionState(const std::string& filePath = kProductionStateFilePath)
    {
        std::vector<ProductionState> result;
        try
        {
            std::string text = detail::ReadFileToString(filePath);
            auto objects = detail::ParseJsonObjectArray(text);
            result.reserve(objects.size());
            for (const auto& obj : objects)
            {
                ProductionState state;
                state.orderId = obj.count("orderId") ? obj.at("orderId") : "";
                state.productionStartAt = obj.count("productionStartAt") ? obj.at("productionStartAt") : "";
                state.productionEndAt = obj.count("productionEndAt") ? obj.at("productionEndAt") : "";
                state.actualQuantity = obj.count("actualQuantity") ? std::stoi(obj.at("actualQuantity")) : 0;
                state.shortage = obj.count("shortage") ? std::stoi(obj.at("shortage")) : 0;
                result.push_back(state);
            }
        }
        catch (...)
        {
            return {};
        }
        return result;
    }
} // namespace data
