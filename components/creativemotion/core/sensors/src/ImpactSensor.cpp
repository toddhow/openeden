#include "opas/sensors/ImpactSensor.hpp"

#include <algorithm>

namespace opas::sensors
{

    ImpactSensor::ImpactSensor(std::size_t maxEvents)
        : capacity_(std::max<std::size_t>(maxEvents, 1u))
        , count_(0)
        , ordered_(capacity_)
    {
    }

    void ImpactSensor::recordImpact(const ImpactEvent& event)
    {
        if (capacity_ == 0)
        {
            return;
        }
        if (count_ < capacity_)
        {
            ordered_[count_++] = event;
        }
        else
        {
            std::copy(ordered_.begin() + 1, ordered_.end(), ordered_.begin());
            ordered_.back() = event;
        }
    }

    std::span<const ImpactEvent> ImpactSensor::impacts() const noexcept
    {
        return std::span<const ImpactEvent>(ordered_.data(), count_);
    }

    std::optional<ImpactEvent> ImpactSensor::latestImpact() const noexcept
    {
        if (count_ > 0)
        {
            return ordered_[count_ - 1];
        }
        return std::nullopt;
    }

    std::optional<ImpactEvent> ImpactSensor::latestPartImpact(arbiter::BodyPart part) const noexcept
    {
        for (std::size_t i = count_; i > 0; --i)
        {
            if (ordered_[i - 1].bodyPart == part)
            {
                return ordered_[i - 1];
            }
        }
        return std::nullopt;
    }

    double ImpactSensor::totalImpulseMagnitude() const noexcept
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < count_; ++i)
        {
            sum += math::length(ordered_[i].impulseNs);
        }
        return sum;
    }

    void ImpactSensor::clear() noexcept
    {
        count_ = 0;
    }

} // namespace opas::sensors
