#ifndef TNC155_PANEL_KEYS_H
#define TNC155_PANEL_KEYS_H

/* Raw P8279 matrix codes as presented to the MAIN firmware.  These are NOT
 * P3's post-translation logical key codes.  P3 maps raw codes >2B..>7F
 * through its table at physical >921C.  Keeping these names explicit avoids
 * the regression where logical codes were injected back into the raw FIFO. */
#define TNC155_RAW_KEY_P          0x43u
#define TNC155_RAW_KEY_I_MODE     0x44u
#define TNC155_RAW_KEY_STOP       0x54u
#define TNC155_RAW_KEY_UP         0x58u
#define TNC155_RAW_KEY_LEFT       0x59u
#define TNC155_RAW_KEY_RIGHT      0x5au
#define TNC155_RAW_KEY_PLUSMINUS  0x64u
#define TNC155_RAW_KEY_ENT        0x65u
#define TNC155_RAW_KEY_DOWN       0x67u
#define TNC155_RAW_KEY_7          0x68u
#define TNC155_RAW_KEY_9          0x69u
#define TNC155_RAW_KEY_6          0x6au
#define TNC155_RAW_KEY_3          0x6bu
#define TNC155_RAW_KEY_END        0x6du
#define TNC155_RAW_KEY_8          0x6eu
#define TNC155_RAW_KEY_5          0x6fu
#define TNC155_RAW_KEY_2          0x70u
#define TNC155_RAW_KEY_DECIMAL    0x71u
#define TNC155_RAW_KEY_4          0x73u
#define TNC155_RAW_KEY_1          0x74u
#define TNC155_RAW_KEY_0          0x75u
#define TNC155_RAW_KEY_X          0x77u
#define TNC155_RAW_KEY_Y          0x7cu
#define TNC155_RAW_KEY_Z          0x7du
#define TNC155_RAW_KEY_IV         0x7eu
#define TNC155_RAW_KEY_CE         0x7fu

#endif
