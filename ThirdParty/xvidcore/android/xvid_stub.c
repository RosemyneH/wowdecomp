/* ʕ •ᴥ•ʔ✿ Android has no system libxvidcore. Cinematics fail closed until a device build exists. ✿ ʕ •ᴥ•ʔ */

int xvid_global(void* handle, int opt, void* param1, void* param2) {
    (void)handle;
    (void)opt;
    (void)param1;
    (void)param2;
    return -1;
}

int xvid_decore(void* handle, int opt, void* param1, void* param2) {
    (void)handle;
    (void)opt;
    (void)param1;
    (void)param2;
    return -1;
}
