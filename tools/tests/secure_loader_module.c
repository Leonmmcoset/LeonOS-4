#ifdef HOSTILE_PRELOAD
int hostile_preloaded = 1;
#else
int secure_loader_dependency(void) { return DEPENDENCY_VALUE; }
#endif
