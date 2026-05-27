#ifndef FILE_HANDLE_H
#define FILE_HANDLE_H

#include "platform.h"

// Move-only RAII wrapper around FILE*. Replaces scattered fopen/fclose pairs
// and the asymmetric unique_ptr<FILE, decltype(&std::fclose)> pattern that
// used to live in fileops.cpp.
class FileHandle {
public:
    FileHandle() = default;
    explicit FileHandle(FILE *f) : fp_(f) {}
    ~FileHandle() { reset(); }

    FileHandle(FileHandle &&o) noexcept : fp_(o.fp_) { o.fp_ = nullptr; }
    FileHandle &operator=(FileHandle &&o) noexcept {
        if (this != &o) {
            reset();
            fp_   = o.fp_;
            o.fp_ = nullptr;
        }
        return *this;
    }

    FileHandle(const FileHandle &)            = delete;
    FileHandle &operator=(const FileHandle &) = delete;

    FILE *get() const            { return fp_; }
    explicit operator bool() const { return fp_ != nullptr; }

    void reset(FILE *f = nullptr) {
        if (fp_) fclose(fp_);
        fp_ = f;
    }

    FILE *release() {
        FILE *f = fp_;
        fp_     = nullptr;
        return f;
    }

private:
    FILE *fp_ = nullptr;
};

#endif
