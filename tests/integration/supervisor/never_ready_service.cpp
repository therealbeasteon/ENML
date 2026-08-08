#include <time.h>

int main() {
    const timespec delay{.tv_sec = 1, .tv_nsec = 0};
    (void)::nanosleep(&delay, nullptr);
    return 0;
}
