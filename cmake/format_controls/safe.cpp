#include <cstdio>

void format_control()
{
    char text[32];
    int speed = 7;
    std::snprintf(text, sizeof(text), "v=%.2f", static_cast<double>(speed));
}
