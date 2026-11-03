/* DirectInput 7 surface used by gta2_re, for the N64 port.
 *
 * Declarations and standard constant values only (scan codes, flags, error
 * codes) so key tables and error handling keep their meaning. The device
 * objects are implemented in src/n64/dinput_n64.cpp.
 */
#pragma once
#include <windows.h>

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0700
#endif

/* Scan codes (Set 1, as DirectInput reports them). */
#define DIK_ESCAPE 0x01
#define DIK_1 0x02
#define DIK_2 0x03
#define DIK_3 0x04
#define DIK_4 0x05
#define DIK_5 0x06
#define DIK_6 0x07
#define DIK_7 0x08
#define DIK_8 0x09
#define DIK_9 0x0A
#define DIK_0 0x0B
#define DIK_MINUS 0x0C
#define DIK_EQUALS 0x0D
#define DIK_BACK 0x0E
#define DIK_BACKSPACE DIK_BACK
#define DIK_TAB 0x0F
#define DIK_Q 0x10
#define DIK_W 0x11
#define DIK_E 0x12
#define DIK_R 0x13
#define DIK_T 0x14
#define DIK_Y 0x15
#define DIK_U 0x16
#define DIK_I 0x17
#define DIK_O 0x18
#define DIK_P 0x19
#define DIK_LBRACKET 0x1A
#define DIK_RBRACKET 0x1B
#define DIK_RETURN 0x1C
#define DIK_LCONTROL 0x1D
#define DIK_A 0x1E
#define DIK_S 0x1F
#define DIK_D 0x20
#define DIK_F 0x21
#define DIK_G 0x22
#define DIK_H 0x23
#define DIK_J 0x24
#define DIK_K 0x25
#define DIK_L 0x26
#define DIK_SEMICOLON 0x27
#define DIK_APOSTROPHE 0x28
#define DIK_GRAVE 0x29
#define DIK_LSHIFT 0x2A
#define DIK_BACKSLASH 0x2B
#define DIK_Z 0x2C
#define DIK_X 0x2D
#define DIK_C 0x2E
#define DIK_V 0x2F
#define DIK_B 0x30
#define DIK_N 0x31
#define DIK_M 0x32
#define DIK_COMMA 0x33
#define DIK_PERIOD 0x34
#define DIK_SLASH 0x35
#define DIK_RSHIFT 0x36
#define DIK_MULTIPLY 0x37
#define DIK_LMENU 0x38
#define DIK_SPACE 0x39
#define DIK_CAPITAL 0x3A
#define DIK_F1 0x3B
#define DIK_F2 0x3C
#define DIK_F3 0x3D
#define DIK_F4 0x3E
#define DIK_F5 0x3F
#define DIK_F6 0x40
#define DIK_F7 0x41
#define DIK_F8 0x42
#define DIK_F9 0x43
#define DIK_F10 0x44
#define DIK_NUMLOCK 0x45
#define DIK_SCROLL 0x46
#define DIK_NUMPAD7 0x47
#define DIK_NUMPAD8 0x48
#define DIK_NUMPAD9 0x49
#define DIK_SUBTRACT 0x4A
#define DIK_NUMPAD4 0x4B
#define DIK_NUMPAD5 0x4C
#define DIK_NUMPAD6 0x4D
#define DIK_ADD 0x4E
#define DIK_NUMPAD1 0x4F
#define DIK_NUMPAD2 0x50
#define DIK_NUMPAD3 0x51
#define DIK_NUMPAD0 0x52
#define DIK_DECIMAL 0x53
#define DIK_F11 0x57
#define DIK_F12 0x58
#define DIK_NUMPADENTER 0x9C
#define DIK_RCONTROL 0x9D
#define DIK_DIVIDE 0xB5
#define DIK_SYSRQ 0xB7
#define DIK_RMENU 0xB8
#define DIK_PAUSE 0xC5
#define DIK_HOME 0xC7
#define DIK_UP 0xC8
#define DIK_PRIOR 0xC9
#define DIK_LEFT 0xCB
#define DIK_RIGHT 0xCD
#define DIK_END 0xCF
#define DIK_DOWN 0xD0
#define DIK_NEXT 0xD1
#define DIK_INSERT 0xD2
#define DIK_DELETE 0xD3

#define DIDF_ABSAXIS 0x00000001
#define DIDF_RELAXIS 0x00000002
#define DIPH_DEVICE 0
#define DIPH_BYOFFSET 1
#define DISCL_EXCLUSIVE 0x00000001
#define DISCL_NONEXCLUSIVE 0x00000002
#define DISCL_FOREGROUND 0x00000004
#define DISCL_BACKGROUND 0x00000008
#define DIGDD_PEEK 0x00000001
#define DIENUM_STOP 0
#define DIENUM_CONTINUE 1
#define DIEDFL_ALLDEVICES 0x00000000
#define DIEDFL_ATTACHEDONLY 0x00000001
#define DIDEVTYPE_DEVICE 1
#define DIDEVTYPE_MOUSE 2
#define DIDEVTYPE_KEYBOARD 3
#define DIDEVTYPE_JOYSTICK 4
#define DIDFT_ALL 0x00000000
#define DIDFT_AXIS 0x00000003
#define DIDFT_BUTTON 0x0000000C
#define DIDFT_ANYINSTANCE 0x00FFFF00
#define DIDFT_OPTIONAL 0x80000000

#define DI_OK S_OK
#define DI_NOTATTACHED S_FALSE
#define DI_BUFFEROVERFLOW S_FALSE
#define DI_PROPNOEFFECT S_FALSE
#define DI_NOEFFECT S_FALSE
#define DI_POLLEDDEVICE ((HRESULT)0x00000002L)
#define DI_DOWNLOADSKIPPED ((HRESULT)0x00000003L)
#define DI_EFFECTRESTARTED ((HRESULT)0x00000004L)
#define DI_TRUNCATED ((HRESULT)0x00000008L)
#define DI_TRUNCATEDANDRESTARTED ((HRESULT)0x0000000CL)
#define DIERR_OLDDIRECTINPUTVERSION ((HRESULT)0x8007047EL)
#define DIERR_BETADIRECTINPUTVERSION ((HRESULT)0x80070481L)
#define DIERR_BADDRIVERVER ((HRESULT)0x80070077L)
#define DIERR_DEVICENOTREG ((HRESULT)0x80040154L)
#define DIERR_NOTFOUND ((HRESULT)0x80070002L)
#define DIERR_OBJECTNOTFOUND ((HRESULT)0x80070002L)
#define DIERR_INVALIDPARAM E_INVALIDARG
#define DIERR_NOINTERFACE E_NOINTERFACE
#define DIERR_GENERIC E_FAIL
#define DIERR_OUTOFMEMORY E_OUTOFMEMORY
#define DIERR_UNSUPPORTED ((HRESULT)0x80004001L)
#define DIERR_NOTINITIALIZED ((HRESULT)0x80070015L)
#define DIERR_ALREADYINITIALIZED ((HRESULT)0x800704DFL)
#define DIERR_NOAGGREGATION ((HRESULT)0x80040110L)
#define DIERR_OTHERAPPHASPRIO ((HRESULT)0x80070005L)
#define DIERR_INPUTLOST ((HRESULT)0x8007001EL)
#define DIERR_ACQUIRED ((HRESULT)0x800700AAL)
#define DIERR_NOTACQUIRED ((HRESULT)0x8007000CL)
#define DIERR_INSUFFICIENTPRIVS ((HRESULT)0x80040200L)
#define DIERR_DEVICEFULL ((HRESULT)0x80040201L)
#define DIERR_MOREDATA ((HRESULT)0x80040202L)
#define DIERR_NOTDOWNLOADED ((HRESULT)0x80040203L)
#define DIERR_HASEFFECTS ((HRESULT)0x80040204L)
#define DIERR_NOTEXCLUSIVEACQUIRED ((HRESULT)0x80040205L)
#define DIERR_INCOMPLETEEFFECT ((HRESULT)0x80040206L)
#define DIERR_NOTBUFFERED ((HRESULT)0x80040207L)
#define DIERR_EFFECTPLAYING ((HRESULT)0x80040208L)

typedef struct DIOBJECTDATAFORMAT { const GUID* pguid; DWORD dwOfs, dwType, dwFlags; } DIOBJECTDATAFORMAT, *LPDIOBJECTDATAFORMAT;
typedef const DIOBJECTDATAFORMAT* LPCDIOBJECTDATAFORMAT;
typedef struct DIDATAFORMAT { DWORD dwSize, dwObjSize, dwFlags, dwDataSize, dwNumObjs; LPDIOBJECTDATAFORMAT rgodf; } DIDATAFORMAT, *LPDIDATAFORMAT;
typedef const DIDATAFORMAT* LPCDIDATAFORMAT;
typedef struct DIDEVICEOBJECTDATA { DWORD dwOfs, dwData, dwTimeStamp, dwSequence; } DIDEVICEOBJECTDATA, *LPDIDEVICEOBJECTDATA;
typedef struct DIPROPHEADER { DWORD dwSize, dwHeaderSize, dwObj, dwHow; } DIPROPHEADER, *LPDIPROPHEADER;
typedef const DIPROPHEADER* LPCDIPROPHEADER;
typedef struct DIPROPDWORD { DIPROPHEADER diph; DWORD dwData; } DIPROPDWORD, *LPDIPROPDWORD;
typedef struct DIPROPRANGE { DIPROPHEADER diph; LONG lMin, lMax; } DIPROPRANGE, *LPDIPROPRANGE;
typedef struct DIDEVICEINSTANCEA
{
    DWORD dwSize;
    GUID guidInstance, guidProduct;
    DWORD dwDevType;
    CHAR tszInstanceName[MAX_PATH], tszProductName[MAX_PATH];
    GUID guidFFDriver;
    WORD wUsagePage, wUsage;
} DIDEVICEINSTANCEA, *LPDIDEVICEINSTANCEA;
typedef const DIDEVICEINSTANCEA* LPCDIDEVICEINSTANCEA;
typedef DIDEVICEINSTANCEA DIDEVICEINSTANCE;
typedef struct DIJOYSTATE { LONG lX, lY, lZ, lRx, lRy, lRz, rglSlider[2]; DWORD rgdwPOV[4]; BYTE rgbButtons[32]; } DIJOYSTATE;
typedef struct DIMOUSESTATE { LONG lX, lY, lZ; BYTE rgbButtons[4]; } DIMOUSESTATE;
#define DIJOFS_X 0
#define DIJOFS_Y 4
#define DIJOFS_Z 8
#define DIJOFS_BUTTON(n) (48 + (n))

typedef BOOL (*LPDIENUMDEVICESCALLBACKA)(LPCDIDEVICEINSTANCEA, LPVOID);
#define LPDIENUMDEVICESCALLBACK LPDIENUMDEVICESCALLBACKA

#ifdef __cplusplus
extern "C" {
#endif
extern const GUID GUID_XAxis, GUID_YAxis, GUID_ZAxis, GUID_RxAxis, GUID_RyAxis, GUID_RzAxis;
extern const GUID GUID_Slider, GUID_Button, GUID_Key, GUID_POV, GUID_Unknown;
extern const GUID GUID_SysMouse, GUID_SysKeyboard, GUID_Joystick;
extern const GUID IID_IDirectInputDevice2A;
/* DIPROP_* are small integers cast to a GUID reference in the real SDK. */
extern const GUID gta2_diprop_guids[8];
#ifdef __cplusplus
}
#endif
#define MAKEDIPROP(n) (gta2_diprop_guids[(n)])
#define DIPROP_BUFFERSIZE MAKEDIPROP(1)
#define DIPROP_AXISMODE MAKEDIPROP(2)
#define DIPROP_GRANULARITY MAKEDIPROP(3)
#define DIPROP_RANGE MAKEDIPROP(4)
#define DIPROP_DEADZONE MAKEDIPROP(5)
#define DIPROP_SATURATION MAKEDIPROP(6)

#ifdef __cplusplus
struct IDirectInputDeviceA : public IUnknown
{
    virtual HRESULT GetCapabilities(LPVOID) = 0;
    virtual HRESULT EnumObjects(LPVOID, LPVOID, DWORD) = 0;
    virtual HRESULT GetProperty(REFGUID, LPDIPROPHEADER) = 0;
    virtual HRESULT SetProperty(REFGUID, LPCDIPROPHEADER) = 0;
    virtual HRESULT Acquire() = 0;
    virtual HRESULT Unacquire() = 0;
    virtual HRESULT GetDeviceState(DWORD, LPVOID) = 0;
    virtual HRESULT GetDeviceData(DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD) = 0;
    virtual HRESULT SetDataFormat(LPCDIDATAFORMAT) = 0;
    virtual HRESULT SetEventNotification(HANDLE) = 0;
    virtual HRESULT SetCooperativeLevel(HWND, DWORD) = 0;
    virtual HRESULT GetObjectInfo(LPVOID, DWORD, DWORD) = 0;
    virtual HRESULT GetDeviceInfo(LPDIDEVICEINSTANCEA) = 0;
    virtual HRESULT RunControlPanel(HWND, DWORD) = 0;
    virtual HRESULT Initialize(HINSTANCE, DWORD, REFGUID) = 0;
};
struct IDirectInputDevice2A : public IDirectInputDeviceA
{
    virtual HRESULT CreateEffect(REFGUID, LPVOID, LPVOID*, LPUNKNOWN) = 0;
    virtual HRESULT EnumEffects(LPVOID, LPVOID, DWORD) = 0;
    virtual HRESULT GetEffectInfo(LPVOID, REFGUID) = 0;
    virtual HRESULT GetForceFeedbackState(LPDWORD) = 0;
    virtual HRESULT SendForceFeedbackCommand(DWORD) = 0;
    virtual HRESULT EnumCreatedEffectObjects(LPVOID, LPVOID, DWORD) = 0;
    virtual HRESULT Escape(LPVOID) = 0;
    virtual HRESULT Poll() = 0;
    virtual HRESULT SendDeviceData(DWORD, LPVOID, LPDWORD, DWORD) = 0;
};
struct IDirectInputA : public IUnknown
{
    virtual HRESULT CreateDevice(REFGUID, IDirectInputDeviceA**, LPUNKNOWN) = 0;
    virtual HRESULT EnumDevices(DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD) = 0;
    virtual HRESULT GetDeviceStatus(REFGUID) = 0;
    virtual HRESULT RunControlPanel(HWND, DWORD) = 0;
    virtual HRESULT Initialize(HINSTANCE, DWORD) = 0;
};
typedef IDirectInputA *LPDIRECTINPUTA, *LPDIRECTINPUT;
typedef IDirectInputDeviceA *LPDIRECTINPUTDEVICEA, *LPDIRECTINPUTDEVICE;
typedef IDirectInputDevice2A *LPDIRECTINPUTDEVICE2A, *LPDIRECTINPUTDEVICE2;
typedef IDirectInputA IDirectInput;
typedef IDirectInputDeviceA IDirectInputDevice;
extern "C" HRESULT DirectInputCreateA(HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN);
#define DirectInputCreate DirectInputCreateA
#endif
