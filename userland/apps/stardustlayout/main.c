extern int stardustui_cpp_main(int argc, char **argv, char **envp)
    __asm__("_Z4mainiPPcS0_");

int main(int argc, char **argv, char **envp)
{
    return stardustui_cpp_main(argc, argv, envp);
}
