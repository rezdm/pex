#pragma once

namespace pex {

class IView {
public:
    virtual ~IView() = default;
    virtual void render() = 0;
};

} // namespace pex
