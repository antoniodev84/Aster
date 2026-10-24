#include "common.h"
int main(void) {
    return output_all(1, "\033[2J\033[H", 7) < 0;
}
