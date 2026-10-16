// ʕ •ᴥ•ʔ✿ Minimal C addon: clang --target=wasm32-unknown-unknown -nostdlib -Wl,--no-entry -Wl,--export-all ✿ ʕ •ᴥ•ʔ
__attribute__((import_module("wow"), import_name("chat_print")))
extern int chat_print(const char* ptr, unsigned int len);
__attribute__((used))
int wow_on_load(void) {
    static const char msg[] = "Hello from C WASM";
    return chat_print(msg, sizeof(msg) - 1);
}
