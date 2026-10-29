#pragma once

#include "radio76/media_types.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace radio76 {

class StreamPlayer {
public:
    explicit StreamPlayer(std::function<void(std::string_view)> trace = {});

    ~StreamPlayer();

    StreamPlayer(const StreamPlayer&) = delete;

    StreamPlayer& operator=(const StreamPlayer&) = delete;

    bool Start(std::string_view address);

    void Stop() noexcept;

    void Shutdown() noexcept;

    [[nodiscard]] bool Active() const noexcept;

    [[nodiscard]] bool Failed() const noexcept;

    [[nodiscard]] PlaybackState State();

    [[nodiscard]] std::string Title() const;

    [[nodiscard]] std::string Station() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
