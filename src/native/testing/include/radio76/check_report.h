#pragma once

#include <iostream>
#include <string>

namespace radio76::testing {

class CheckReport {
public:
    void Check(bool condition, const std::string& label) {
        ++checks_;
        if (condition) {
            std::cout << "PASS " << label << '\n';
        } else {
            ++failures_;
            std::cout << "FAIL " << label << '\n';
        }
    }

    void Info(const std::string& label) const {
        std::cout << "INFO " << label << '\n';
    }

    [[nodiscard]] int Checks() const noexcept {
        return checks_;
    }

    [[nodiscard]] int Failures() const noexcept {
        return failures_;
    }

    [[nodiscard]] int ExitCode() const noexcept {
        return failures_ == 0 ? 0 : 1;
    }

private:
    int checks_ = 0;
    int failures_ = 0;
};

}
