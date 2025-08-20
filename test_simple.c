/* 简单测试文件，避免复杂的内核依赖 */

static int global_counter = 0;
static char global_buffer[256];

void simple_handler(void) {
    global_counter++;
    global_buffer[0] = 'x';
}

void another_function(int param) {
    if (param > 0) {
        simple_handler();
    }
    global_counter = param;
}

int main(void) {
    another_function(42);
    return 0;
}
