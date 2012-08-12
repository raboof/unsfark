
#ifdef WIN32
#else
#define WINAPI
#endif

typedef void * SFARKHANDLE;

void * WINAPI SfarkErrMsg(SFARKHANDLE sf, int code);

unsigned int WINAPI SfarkPercent(SFARKHANDLE sf);

int WINAPI SfarkBeginExtract(SFARKHANDLE sf, const void * sfontName);
int WINAPI SfarkExtract(SFARKHANDLE sf);

int WINAPI SfarkOpen(SFARKHANDLE sf, const void * sfarkName);
void WINAPI SfarkClose(SFARKHANDLE sf);

void * WINAPI SfarkGetBuffer(SFARKHANDLE sf);

void * WINAPI SfarkAlloc(void);
void WINAPI SfarkFree(SFARKHANDLE sf);
