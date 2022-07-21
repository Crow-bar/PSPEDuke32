
#ifndef __joystick_h
#define __joystick_h

#ifdef __cplusplus
extern "C" {
#endif

#if defined(GEKKO)
#define WII_A           0x00000001
#define WII_B           0x00000002
#define WII_1           0x00000004
#define WII_2           0x00000008
#define WII_MINUS       0x00000010
#define WII_PLUS        0x00000020
#define WII_HOME        0x00000040
#define WII_Z           0x00000080
#define WII_C           0x00000100
#define WII_X           0x00000200
#define WII_Y           0x00000400
#define WII_FULL_L      0x00000800
#define WII_FULL_R      0x00001000
#define WII_ZL          0x00002000
#define WII_ZR          0x00004000
#define WII_DPAD_UP     0x00008000
#define WII_DPAD_RIGHT  0x00010000
#define WII_DPAD_DOWN   0x00020000
#define WII_DPAD_LEFT   0x00040000
#endif

#if defined(__PSP__)
#define PSP_SELECT      0x00000001
#define PSP_START       0x00000002
#define PSP_UP          0x00000004
#define PSP_RIGHT       0x00000008
#define PSP_DOWN        0x00000010
#define PSP_LEFT        0x00000020
#define PSP_LTRIGGER    0x00000040
#define PSP_RTRIGGER    0x00000080
#define PSP_TRIANGLE    0x00000100
#define PSP_CIRCLE      0x00000200
#define PSP_CROSS       0x00000400
#define PSP_SQUARE      0x00000800
#define PSP_HOME        0x00001000
#define PSP_HOLD        0x00002000
#define PSP_NOTE        0x00004000
#define PSP_SCREEN      0x00008000
#define PSP_VOLUP       0x00010000
#define PSP_VOLDOWN     0x00020000
#define PSP_WLAN_UP     0x00040000
#define PSP_REMOTE      0x00080000
#define PSP_DISC        0x00100000
#define PSP_MS          0x00200000
#endif

#define HAT_CENTERED    0x00
#define HAT_UP          0x01
#define HAT_RIGHT       0x02
#define HAT_DOWN        0x04
#define HAT_LEFT        0x08
#define HAT_RIGHTUP	    (HAT_RIGHT|HAT_UP)
#define HAT_RIGHTDOWN   (HAT_RIGHT|HAT_DOWN)
#define HAT_LEFTUP      (HAT_LEFT|HAT_UP)
#define HAT_LEFTDOWN    (HAT_LEFT|HAT_DOWN)

int32_t	JOYSTICK_GetButtons( void );
int32_t	JOYSTICK_ClearButton( int32_t b );
void	JOYSTICK_ClearAllButtons( void );

int32_t	JOYSTICK_GetHat( int32_t h );
void	JOYSTICK_ClearHat( int32_t h );
void	JOYSTICK_ClearAllHats( void );

int32_t	JOYSTICK_GetAxis( int32_t a );
void	JOYSTICK_ClearAxis( int32_t a );
void	JOYSTICK_ClearAllAxes( void );

#ifdef __cplusplus
}
#endif
#endif /* __joystick_h */
