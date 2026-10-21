#include "rg/core/model.hpp"

#include <algorithm>
#include <stdexcept>

namespace rg::core {

Num& node_beat(Node* n) {
    switch (n->kind) {
        case Kind::Bpm:
            return static_cast<Bpm*>(n)->beat;
        case Kind::TimeScalePoint:
            return static_cast<TimeScalePoint*>(n)->beat;
        case Kind::Volume:
            return static_cast<Volume*>(n)->beat;
        case Kind::Single:
            return static_cast<Single*>(n)->beat;
        case Kind::Skill:
            return static_cast<Skill*>(n)->beat;
        case Kind::FeverChance:
            return static_cast<FeverChance*>(n)->beat;
        case Kind::FeverStart:
            return static_cast<FeverStart*>(n)->beat;
        case Kind::HolodoriSkill:
            return static_cast<HolodoriSkill*>(n)->beat;
        case Kind::HolodoriChargeStart:
            return static_cast<HolodoriChargeStart*>(n)->beat;
        case Kind::HolodoriChargeEnd:
            return static_cast<HolodoriChargeEnd*>(n)->beat;
        case Kind::HolodoriFeverStart:
            return static_cast<HolodoriFeverStart*>(n)->beat;
        case Kind::HolodoriFeverEnd:
            return static_cast<HolodoriFeverEnd*>(n)->beat;
        case Kind::SlideStartPoint:
            return static_cast<SlideStartPoint*>(n)->beat;
        case Kind::SlideRelayPoint:
            return static_cast<SlideRelayPoint*>(n)->beat;
        case Kind::SlideEndPoint:
            return static_cast<SlideEndPoint*>(n)->beat;
        case Kind::GuidePoint:
            return static_cast<GuidePoint*>(n)->beat;
        default:
            throw std::runtime_error("node_beat: node has no beat");
    }
}

double node_lane(const Node* n) {
    switch (n->kind) {
        case Kind::Single:
            return static_cast<const Single*>(n)->lane;
        case Kind::SlideStartPoint:
            return static_cast<const SlideStartPoint*>(n)->lane;
        case Kind::SlideRelayPoint:
            return static_cast<const SlideRelayPoint*>(n)->lane;
        case Kind::SlideEndPoint:
            return static_cast<const SlideEndPoint*>(n)->lane;
        case Kind::GuidePoint:
            return static_cast<const GuidePoint*>(n)->lane;
        default:
            throw std::runtime_error("node_lane: node has no lane");
    }
}

double node_size(const Node* n) {
    switch (n->kind) {
        case Kind::Single:
            return static_cast<const Single*>(n)->size;
        case Kind::SlideStartPoint:
            return static_cast<const SlideStartPoint*>(n)->size;
        case Kind::SlideRelayPoint:
            return static_cast<const SlideRelayPoint*>(n)->size;
        case Kind::SlideEndPoint:
            return static_cast<const SlideEndPoint*>(n)->size;
        case Kind::GuidePoint:
            return static_cast<const GuidePoint*>(n)->size;
        default:
            throw std::runtime_error("node_size: node has no size");
    }
}

void Slide::append(NodePtr slidepoint) {
    connections.push_back(std::move(slidepoint));
    sort();
}

void Slide::sort() {
    std::stable_sort(connections.begin(), connections.end(),
                     [](const NodePtr& a, const NodePtr& b) {
                         return node_beat(a.get()) < node_beat(b.get());
                     });
}

void Guide::append(shared_ptr<GuidePoint> guidepoint) {
    midpoints.push_back(std::move(guidepoint));
    sort();
}

void Guide::sort() {
    std::stable_sort(midpoints.begin(), midpoints.end(),
                     [](const shared_ptr<GuidePoint>& a, const shared_ptr<GuidePoint>& b) {
                         return a->beat < b->beat;
                     });
}

}  // namespace rg::core
