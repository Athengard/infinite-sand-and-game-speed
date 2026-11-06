#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
static unsigned long long (WINAPI *realTick64)(void);
#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

static HMODULE selfmod;
static CRITICAL_SECTION lock;
static double multiplier=1.0;
static LONGLONG qbase, qvirtual;
static ULONGLONG tbase, tvirtual;
static int speedReady=0, sandOn=0;
static BYTE *sandSite, *sandStub;
static BYTE original[7]={0xf3,0x43,0x0f,0x10,0x74,0xa0,0x0c};
static HWND statusLabel, sandButton, window;
static void setSpeed(int m);
static int speedChoice=1;
/* Minimal XInput ABI; poll without consuming the game's own button events. */
typedef struct { WORD buttons; BYTE leftTrigger, rightTrigger; SHORT lx, ly, rx, ry; } PadGamepad;
typedef struct { DWORD packet; PadGamepad gamepad; } PadState;
typedef DWORD (WINAPI *PadGetState)(DWORD, PadState *);
static PadGetState padGetState;
static WORD padPrevious[4];
static BYTE padConnected[4];
#define PAD_LEFT_THUMB 0x0040
#define PAD_A 0x1000
#define PAD_Y 0x8000
static int padAction(WORD prev, WORD now) {
 if (!(now & PAD_LEFT_THUMB)) return 0;
 if ((now & PAD_A) && !(prev & PAD_A)) return 2;
 if ((now & PAD_Y) && !(prev & PAD_Y)) return 1;
 return 0;
}
static int nextSpeed(int speed) { return speed < 2 ? 2 : speed < 3 ? 3 : 5; }
#ifdef MOD_TEST
__declspec(dllexport) int testPadAction(WORD prev, WORD now) { return padAction(prev,now); }
__declspec(dllexport) int testNextSpeed(int value) { return nextSpeed(value); }
#endif
static void updateText(void);
static void initGamepad(void) {
 static const WCHAR *names[]={L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"};
 WCHAR path[MAX_PATH]; UINT n=GetSystemDirectoryW(path,MAX_PATH); int i;
 if (!n || n+20>=MAX_PATH) return;
 for(i=0;i<3;i++) { HMODULE mod; int j=0; path[n]=L'\\';
  do { path[n+1+j]=names[i][j]; } while(names[i][j++]);
  mod=LoadLibraryW(path); if(mod){padGetState=(PadGetState)GetProcAddress(mod,"XInputGetState"); if(padGetState)return; FreeLibrary(mod);}
 }
}
static void pollGamepad(void) {
 DWORD foregroundPid=0; HWND foreground=GetForegroundWindow(); int i;
 if (!padGetState) return;
 if (foreground) GetWindowThreadProcessId(foreground,&foregroundPid);
 for(i=0;i<4;i++) { PadState state; WORD buttons; int action;
  if(padGetState(i,&state)!=ERROR_SUCCESS){padConnected[i]=0;padPrevious[i]=0;continue;}
  buttons=state.gamepad.buttons;
  action=padConnected[i] ? padAction(padPrevious[i],buttons) : 0;
  padConnected[i]=1;padPrevious[i]=buttons;
  if(foregroundPid!=GetCurrentProcessId() || !speedReady)continue;
  if(action==2){setSpeed(1);updateText();} else if(action==1){setSpeed(nextSpeed(speedChoice));updateText();}
 }
}

static int same(const char *a,const char *b) { while(*a && *a==*b){a++;b++;} return *a==*b; }
static int bytesame(const BYTE *a,const BYTE *b,int n){int i;for(i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
static BOOL WINAPI hookQpc(LARGE_INTEGER *out) { LARGE_INTEGER now; QueryPerformanceCounter(&now); EnterCriticalSection(&lock); out->QuadPart=qvirtual+(LONGLONG)((now.QuadPart-qbase)*multiplier); LeaveCriticalSection(&lock); return TRUE; }
static ULONGLONG clockValue(void) { ULONGLONG now=realTick64(), r; EnterCriticalSection(&lock); r=tvirtual+(ULONGLONG)((now-tbase)*multiplier); LeaveCriticalSection(&lock); return r; }
static ULONGLONG WINAPI hookTick64(void){return clockValue();}
static DWORD WINAPI hookTick(void){return (DWORD)clockValue();}
static DWORD WINAPI hookTime(void){return (DWORD)clockValue();}
static void setSpeed(int m){ LARGE_INTEGER q; ULONGLONG t; QueryPerformanceCounter(&q);t=realTick64();EnterCriticalSection(&lock); qvirtual+=(LONGLONG)((q.QuadPart-qbase)*multiplier);tvirtual+=(ULONGLONG)((t-tbase)*multiplier); qbase=q.QuadPart;tbase=t;multiplier=m;LeaveCriticalSection(&lock);speedChoice=m; }
static int patchImports(void) {
 BYTE *base=(BYTE*)GetModuleHandleW(NULL);IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER*)base;IMAGE_NT_HEADERS64 *nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);DWORD va=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;IMAGE_IMPORT_DESCRIPTOR *d;int count=0;
 if(!va)return 0;d=(IMAGE_IMPORT_DESCRIPTOR*)(base+va);
 for(;d->Name;d++) { IMAGE_THUNK_DATA64 *n,*f; if(!d->OriginalFirstThunk)continue;n=(IMAGE_THUNK_DATA64*)(base+d->OriginalFirstThunk);f=(IMAGE_THUNK_DATA64*)(base+d->FirstThunk);
  for(;n->u1.AddressOfData;n++,f++) { const char *name;void *replacement=0;DWORD old;
   if(IMAGE_SNAP_BY_ORDINAL64(n->u1.Ordinal))continue;name=(char*)((IMAGE_IMPORT_BY_NAME*)(base+n->u1.AddressOfData))->Name;
   if(same(name,"QueryPerformanceCounter"))replacement=(void*)hookQpc; else if(same(name,"GetTickCount64"))replacement=(void*)hookTick64; else if(same(name,"GetTickCount"))replacement=(void*)hookTick; else if(same(name,"timeGetTime"))replacement=(void*)hookTime;
   if(replacement && VirtualProtect(&f->u1.Function,sizeof(void*),PAGE_READWRITE,&old)) { InterlockedExchangePointer((PVOID volatile*)&f->u1.Function,replacement);VirtualProtect(&f->u1.Function,sizeof(void*),old,&old);count++; }
  }
 } return count;
}
static BYTE *allocNear(BYTE *site){ SYSTEM_INFO si; uintptr_t p=(uintptr_t)site,lo,hi,cur; MEMORY_BASIC_INFORMATION mi;GetSystemInfo(&si);lo=p>0x70000000ULL?p-0x70000000ULL:0x10000;hi=p+0x70000000ULL;cur=lo;while(cur<hi){if(!VirtualQuery((void*)cur,&mi,sizeof(mi)))break; if(mi.State==MEM_FREE){uintptr_t x=((uintptr_t)mi.BaseAddress+si.dwAllocationGranularity-1)&~((uintptr_t)si.dwAllocationGranularity-1); if(x+4096 <= (uintptr_t)mi.BaseAddress+mi.RegionSize){BYTE *a=(BYTE*)VirtualAlloc((void*)x,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);if(a)return a;}} if((uintptr_t)mi.BaseAddress+mi.RegionSize<=cur)break;cur=(uintptr_t)mi.BaseAddress+mi.RegionSize;}return 0;}
static int findSand(void){ BYTE *base=(BYTE*)GetModuleHandleW(NULL);IMAGE_NT_HEADERS64 *nt=(IMAGE_NT_HEADERS64*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);IMAGE_SECTION_HEADER *sec=IMAGE_FIRST_SECTION(nt);int i,matches=0;DWORD j;for(i=0;i<nt->FileHeader.NumberOfSections;i++,sec++){if(!(sec->Characteristics&IMAGE_SCN_MEM_EXECUTE))continue;for(j=0;j+7<=sec->Misc.VirtualSize;j++){BYTE *p=base+sec->VirtualAddress+j;if(bytesame(p,original,7)){sandSite=p;matches++;}}}return matches==1;}
static void copy(BYTE *out,const BYTE *in,int n){int i;for(i=0;i<n;i++)out[i]=in[i];}
static int setupSand(void){
 /* Preserve flags and xmm0; interpolate each cell between its empty and full heights. */
 static const BYTE prefix[]={0x9c,0x83,0x3d,0,0,0,0,0,0x74,0x00,0x9d,0x48,0x8d,0x64,0x24,0xf0,0x0f,0x11,0x04,0x24,0xf3,0x43,0x0f,0x10,0x34,0xa0,0xf3,0x43,0x0f,0x10,0x44,0xa0,0x08,0xf3,0x0f,0x5c,0xc6,0xf3,0x0f,0x59,0x05,0,0,0,0,0xf3,0x0f,0x58,0xf0,0xf3,0x43,0x0f,0x11,0x74,0xa0,0x0c,0x0f,0x10,0x04,0x24,0x48,0x8d,0x64,0x24,0x10,0xeb,0x01,0x9d,0xf3,0x43,0x0f,0x10,0x74,0xa0,0x0c,0xff,0x25,0,0,0,0,0,0,0,0,0,0,0,0};
 DWORD old; int32_t rel; uintptr_t dest; int i; if(!findSand())return 0;sandStub=allocNear(sandSite);if(!sandStub)return 0;copy(sandStub,prefix,sizeof(prefix));
 /* conditional branch skips the enabled block; flag and ratio live after the code */
 sandStub[9]=(BYTE)(67-10);rel=(int32_t)(96-8);copy(sandStub+3,(BYTE*)&rel,4);rel=(int32_t)(100-45);copy(sandStub+41,(BYTE*)&rel,4);dest=(uintptr_t)(sandSite+7);copy(sandStub+81,(BYTE*)&dest,8);sandStub[96]=0;{float x=0.7f;copy(sandStub+100,(BYTE*)&x,4);}
 rel=(int32_t)(sandStub-(sandSite+5));if(!VirtualProtect(sandSite,7,PAGE_EXECUTE_READWRITE,&old))return 0; sandSite[0]=0xe9;copy(sandSite+1,(BYTE*)&rel,4);sandSite[5]=0x90;sandSite[6]=0x90;FlushInstructionCache(GetCurrentProcess(),sandSite,7);VirtualProtect(sandSite,7,old,&old);return 1;
}
static void updateText(void){ WCHAR text[128];wsprintfW(text,L"Speed: x%d    Sand: %s",speedChoice,sandOn?L"70%":L"OFF");SetWindowTextW(statusLabel,text);SetWindowTextW(sandButton,sandOn?L"Sand 70%: ON":L"Sand 70%: OFF");}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){ if(m==WM_TIMER && w==1){pollGamepad();return 0;}if(m==WM_COMMAND){int id=LOWORD(w);if(id>=101&&id<=104){int val=id==101?1:id==102?2:id==103?3:5;if(speedReady)setSpeed(val);}if(id==105&&sandStub){sandOn=!sandOn;InterlockedExchange((LONG*)(sandStub+96),sandOn);}updateText();return 0;}if(m==WM_CLOSE){ShowWindow(h,SW_HIDE);return 0;}if(m==WM_HOTKEY){if(IsWindowVisible(h))ShowWindow(h,SW_HIDE);else{ShowWindow(h,SW_SHOW);SetForegroundWindow(h);}return 0;}return DefWindowProcW(h,m,w,l);}
static DWORD WINAPI run(void *unused){ WNDCLASSW wc;MSG msg;LARGE_INTEGER q;ULONGLONG t;int i; static const wchar_t *labels[]={L"x1",L"x2",L"x3",L"x5"};InitializeCriticalSection(&lock);realTick64=(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetTickCount64");if(!realTick64)return 0;QueryPerformanceCounter(&q);t=realTick64();qbase=qvirtual=q.QuadPart;tbase=tvirtual=t;speedReady=(patchImports()==4);ZeroMemory(&wc,sizeof(wc));wc.lpfnWndProc=proc;wc.hInstance=selfmod;wc.lpszClassName=L"RoadcraftModWindow";wc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);window=CreateWindowExW(WS_EX_TOPMOST,L"RoadcraftModWindow",L"RoadCraft Mod",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,390,220,0,0,selfmod,0);statusLabel=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,15,15,340,24,window,0,selfmod,0);for(i=0;i<4;i++)CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,15+i*85,50,75,30,window,(HMENU)(uintptr_t)(101+i),selfmod,0);sandButton=CreateWindowW(L"BUTTON",L"Sand 70%: OFF",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,15,92,330,30,window,(HMENU)105,selfmod,0);CreateWindowW(L"STATIC",L"F10: open / hide menu",WS_CHILD|WS_VISIBLE,15,130,330,24,window,0,selfmod,0);if(!speedReady)SetWindowTextW(statusLabel,L"Speed unavailable: incompatible imports");if(!setupSand()){EnableWindow(sandButton,FALSE);SetWindowTextW(sandButton,L"Sand unavailable: incompatible game");}if(speedReady)updateText();RegisterHotKey(window,1,MOD_NOREPEAT,VK_F10);initGamepad();if(padGetState)SetTimer(window,1,30,0);while(GetMessageW(&msg,0,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;}
__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE h,DWORD version,const void *iid,LPVOID *out,void *outer){ static HMODULE real=0;typedef HRESULT (WINAPI *fn)(HINSTANCE,DWORD,const void*,LPVOID*,void*); fn f; if(!real){WCHAR path[MAX_PATH];UINT n=GetSystemDirectoryW(path,MAX_PATH);const WCHAR suffix[]=L"\\dinput8.dll";unsigned i=0;if(!n||n+14>=MAX_PATH)return E_FAIL;while((path[n+i]=suffix[i])!=0)i++;real=LoadLibraryW(path);}if(!real)return E_FAIL;f=(fn)GetProcAddress(real,"DirectInput8Create");return f?f(h,version,iid,out,outer):E_FAIL;}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID p){if(reason==DLL_PROCESS_ATTACH){HANDLE thread;selfmod=h;DisableThreadLibraryCalls(h);thread=CreateThread(0,0,run,0,0,0);if(thread)CloseHandle(thread);}return TRUE;}
