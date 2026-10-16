static char keep;

__attribute__((export_name("wow_on_load")))
int wow_on_load(void) {
    return (int)keep;
}
