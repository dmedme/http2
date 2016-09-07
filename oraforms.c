/*
 * Routines for decoding ORACLE forms stuff passed in from ungz or autoscript
 * or whatever.
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (c) E2 Systems 1996";

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "scripttree.h"
void do_ora_forms();
unsigned char * ready_code();
void block_enc_dec();
static struct frame_con * cur_frame;
static void do_http();
static void http_client();
static void http_server();
static unsigned char * forms60_property();
static unsigned char * outstr();
unsigned char * forms60_handle();
/*
 * Structure allocated when a session is started that holds Web session state.
 */
struct ora_web_sess {
    unsigned char * gday;
    int plain_flag;
    int pragma_cnt[2];
    unsigned char * f_enc_dec[2];        /* Encrypt/Decrypt control blocks */
};
struct ora_web_sess * ap;
static struct or_obj_id {
int id;
char * name;
} or_obj_id[] = {
{1,"oracle.forms.engine.Runform"},
{4,"oracle.forms.handler.FormWindow"},
{5,"oracle.forms.handler.AlertDialog"},
{6,"oracle.forms.handler.DisplayList"},
{7,"oracle.forms.handler.LogonDialog"},
{8,"oracle.forms.handler.DisplayErrorDialog"},
{9,"oracle.forms.handler.ListValuesDialog"},
{10,"oracle.forms.handler.EditorDialog"},
{11,"oracle.forms.handler.HelpDialog"},
{12,"oracle.forms.handler.FormStatusBar"},
{13,"oracle.forms.handler.MenuInfo"},
{14,"UNUSED"},
{15,"oracle.forms.handler.ApplicationTimer"},
{16,"oracle.forms.handler.MenuParametersDialog"},
{17,"oracle.forms.handler.PromptListItem"},
{18,"oracle.forms.handler.CancelQueryDialog"},
{257,"oracle.forms.handler.TextFieldItem"},
{258,"oracle.forms.handler.TextAreaItem"},
{259,"oracle.forms.handler.FormCanvas"},
{261,"oracle.forms.handler.ButtonItem"},
{262,"oracle.forms.handler.CheckboxItem"},
{263,"oracle.forms.handler.PopListItem"},
{264,"oracle.forms.handler.TListItem"},
{265,"oracle.forms.handler.CfmVBX"},
{266,"oracle.forms.handler.CfmOLE"},
{267,"oracle.forms.handler.RadioButtonItem"},
{268,"oracle.forms.handler.ImageItem"},
{269,"oracle.forms.handler.IconicButtonItem"},
{270,"oracle.forms.handler.BlockScroller"},
{271,"oracle.forms.handler.JavaContainer"},
{272,"oracle.forms.handler.TabControl"},
{273,"oracle.forms.handler.ComboBoxItem"},
{274,"oracle.forms.handler.TreeItem"},
{281,"oracle.forms.handler.PopupHelpItem"}};
static char* find_name(match_id)
int match_id;
{
struct or_obj_id*guess;
struct or_obj_id* low = &or_obj_id[0];
struct or_obj_id* high =
            &or_obj_id[sizeof(or_obj_id)/sizeof(struct or_obj_id) - 1];

    while (low <= high)
    {
        guess = low + ((high - low) >> 1);
        if ( guess->id == match_id)
            return guess->name;
        else
        if ( guess->id < match_id)
            low = guess + 1;
        else
            high = guess - 1;
    }
    return "(unknown)";
}
static struct or_prop_id {
int id;
char * name;
} or_prop_id[] = {
{101, "APPLICATION"},
{102, "FORM_MODULE"},
{103, "NAME"},
{104, "PARENT"},
{105, "CANCEL"},
{106, "CANCEL_LABEL"},
{107, "CANCEL_MNEMONIC"},
{108, "COLTITLE"},
{109, "COLWIDTH"},
{110, "CONNECT_LABEL"},
{111, "CONNECT_MNEMONIC"},
{112, "FIND_LABEL"},
{113, "FIND_MNEMONIC"},
{114, "ICON_FILENAME"},
{115, "ALIGNMENT"},
{116, "LABEL"},
{117, "MAX_LENGTH"},
{118, "NO_LABEL"},
{119, "OK_LABEL"},
{120, "OK_MNEMONIC"},
{121, "RANGE"},
{122, "ROWVAL"},
{123, "SCROLL"},
{124, "SEARCH_LABEL"},
{125, "SEARCH_MNEMONIC"},
{126, "SIZE"},
{127, "STATUS"},
{128, "STYLE"},
{129, "TITLE"},
{130, "CANVASTYPE"},
{130, "TYPE"},
{131, "VALUE"},
{132, "WINDOW"},
{133, "CLEARDAMAGE"},
{134, "UI_PARENT"},
{135, "LOCATION"},
{136, "INNERSIZE"},
{137, "OUTERSIZE"},
{138, "CANVASMOVABLE"},
{139, "CANVASORIGIN"},
{140, "MNEMONIC"},
{141, "NAVIGABLE"},
{142, "TABSTOP"},
{143, "GROUP"},
{144, "ENABLED"},
{145, "EVENTMODE"},
{146, "FOREGROUND"},
{147, "BACKGROUND"},
{148, "BGPATTERN"},
{149, "FONT"},
{150, "BORDERUSE"},
{151, "BORDERWD"},
{152, "BORDER_BEVEL"},
{153, "BOUNDS"},
{154, "BORDER"},
{155, "VISIBLERECT"},
{156, "ISMAPPED"},
{157, "ISVISIBLE"},
{158, "USAGE"},
{159, "BASELINE"},
{160, "PRIVATE"},
{161, "HIGHLIGHTFOCUS"},
{162, "BMUNPROTECTED"},
{163, "EVENTMASK"},
{164, "MAPREQUEST"},
{165, "LANGUAGE_DIRECTION"},
{166, "DIRCHANGE"},
{167, "VALIDKEYS"},
{168, "DEFAULTKEYS"},
{169, "DRAWTHRU"},
{170, "CANVASHANDLE"},
{171, "DLGOK"},
{172, "MESSAGE"},
{173, "VISIBLE"},
{174, "FOCUS"},
{175, "KEY"},
{176, "SKEY"},
{177, "MOVEABOVE"},
{178, "MOVETOP"},
{179, "DAMAGE"},
{180, "MOUSEDOWN"},
{181, "MOUSEUP"},
{182, "MOUSEENTER"},
{183, "MOUSEEXIT"},
{184, "MOUSEMOVE"},
{185, "MOUSEDOWN_POS"},
{186, "MOUSEDOWN_MOD"},
{187, "AUTOSCROLL"},
{188, "OBSCURE"},
{189, "OPTIMIZED_FOCUS"},
{190, "SPCL_FOREGROUND"},
{191, "SPCL_BACKGROUND"},
{192, "CWIDTH"},
{193, "CURSOR_POSITION"},
{194, "EDITABLE"},
{195, "SELECTION"},
{196, "ALWAYSHOME"},
{197, "SECURE"},
{198, "AUTOSKIP"},
{199, "CASE_FOLD"},
{200, "TYPEOVERMODE"},
{201, "RENDER"},
{202, "CAN_TAKE_FOCUS"},
{203, "HAS_LOV"},
{204, "REQUIRED_CHANGE"},
{205, "REPLACES"},
{206, "CLRDIRTY"},
{207, "CHEIGHT"},
{208, "WRAP_STYLE"},
{209, "VSBARPOLICY"},
{210, "CONTENT"},
{211, "DRAWN_AUTOSCROLLED"},
{212, "DRAWN_SLOWDRAW"},
{213, "DRAWN_CANVASUSAGE"},
{214, "DRAWN_RESIZED"},
{215, "DRAWN_MDIFLAG"},
{216, "WINDOW_CLOSE"},
{217, "WINDOW_CONTENT"},
{218, "WINDOW_HSA"},
{219, "WINDOW_VSA"},
{220, "WINDOW_HTBA"},
{221, "WINDOW_VTBA"},
{222, "WINDOW_MODALITY"},
{223, "WINDOW_MOUSERELATIVE"},
{224, "WINDOW_MIN_SIZE"},
{225, "WINDOW_MAX_SIZE"},
{226, "WINDOW_INC_SIZE"},
{227, "WINDOW_CAN_RESIZE"},
{228, "WINDOW_CAN_CLOSE"},
{229, "WINDOW_CAN_MOVE"},
{230, "WINDOW_CAN_MINIMIZE"},
{231, "WINDOW_CAN_MAXIMIZE"},
{232, "WINDOW_CAN_TITLE"},
{233, "WINDOW_ICON_IMAGE"},
{234, "WINDOW_APP_MENU"},
{235, "WINDOW_NAVIGATION"},
{236, "WINDOW_CURSOR"},
{236, "WINSYS_CURSOR"},
{237, "WINDOW_COLORPAL"},
{238, "WINDOW_COLORMAPREDRAW"},
{239, "WINDOW_MENU"},
{240, "WINDOW_BMPAINTABLE"},
{241, "WINDOW_LEADER"},
{242, "WINDOW_MAXIMIZED"},
{243, "WINDOW_ICONIFIED"},
{244, "WINDOW_USE3D"},
{245, "WINDOW_PRINT"},
{246, "WINDOW_RAISE"},
{247, "WINDOW_ACTIVATED"},
{248, "WINDOW_MDI_VTOOLBAR"},
{249, "WINDOW_MDI_HTOOLBAR"},
{250, "SB_SIZE"},
{251, "SB_LINEINCR"},
{252, "SB_PAGEINCR"},
{253, "SB_SCROLLEE"},
{254, "SB_HORIZONTAL"},
{255, "SB_REVERSEDIR"},
{256, "SB_ACTION"},
{257, "SBOX_CONTENT"},
{258, "SBOX_HSA"},
{259, "SBOX_VSA"},
{260, "SBOX_CONTENTSIZE"},
{261, "SBOX_HLABEL"},
{262, "SBOX_VLABEL"},
{263, "INITIAL_RESOLUTION"},
{264, "INITIAL_DISP_SIZE"},
{265, "INITIAL_CMDLINE"},
{266, "INITIAL_COLOR_DEPTH"},
{267, "INITIAL_SCALE_INFO"},
{268, "INITIAL_VERSION"},
{269, "VERSION_TOO_OLD"},
{270, "VERSION_TOO_NEW"},
{271, "INITIAL_ENCRYPTKEY"},
{272, "WINSYS_CURKEYCS"},
{273, "WINSYS_TERMINALFILE"},
{274, "WINSYS_DEVICENAME"},
{275, "WINSYS_NEEDAPPMENU"},
{276, "WINSYS_SDI"},
{277, "WINSYS_TRANSIENTMENU"},
{278, "WINSYS_BOOTKEYCS"},
{279, "WINSYS_SINGLEBYTECS"},
{280, "WINSYS_DRAWINGDIR"},
{281, "WINSYS_LAYOUTDIR"},
{282, "WINSYS_BEEP"},
{283, "WINSYS_HYPERLINK"},
{284, "WINSYS_COLOR_ADD"},
{285, "WINSYS_CONSOLE_MSG"},
{286, "WINSYS_WORKING_MSG"},
{287, "WINSYS_FONT_ADD"},
{288, "WINSYS_PLAY_TEST"},
{289, "WINSYS_RUNPRODUCT"},
{290, "WINSYS_LOCALESUPPORT"},
{291, "WINSYS_REQUIREDVA_LIST"},
{292, "WINSYS_SYNC_BUILTIN"},
{293, "ALERT_NO_MNEMONIC"},
{294, "ALERT_ICON"},
{295, "ALERT_BUTTONS"},
{296, "ALERT_DEFAULT_BUTTON"},
{297, "BP_TYPE"},
{298, "BP_PARENT"},
{299, "BP_STRINGVAL"},
{300, "BP_BASELINE"},
{301, "BP_HALIGN"},
{302, "BP_VALIGN"},
{303, "BP_LSTART"},
{304, "BP_LEND"},
{305, "BP_FOREFILLCOL"},
{306, "BP_BACKFILLCOL"},
{307, "BP_FOREEDGECOL"},
{308, "BP_BACKEDGECOL"},
{309, "BP_TEXTCOL"},
{310, "BP_LINETHICK"},
{311, "BP_RRECT"},
{312, "BP_EDGEPATTERN"},
{313, "BP_FILLPATTERN"},
{314, "BP_LINE_BEVEL"},
{315, "BP_FONT"},
{316, "BP_NUMCHUNKS"},
{317, "BP_CHUNK"},
{318, "BP_SCREEN_HORIZONTAL"},
{319, "BP_SCREEN_VERTICAL"},
{320, "BP_VGS_HORIZONTAL"},
{321, "BP_VGS_VERTICAL"},
{322, "BP_VGS_VERSION"},
{323, "IS_CANCEL"},
{324, "IS_DEFAULT"},
{325, "PRESSED"},
{326, "IMAGE"},
{327, "PLIST_LISTINDEX"},
{328, "PLIST_LISTITEM"},
{329, "PLIST_ADDLISTITEM"},
{330, "PLIST_DELLISTITEM"},
{331, "PLIST_DELLIST"},
{332, "PLIST_LISTCLOSED"},
{333, "COUNT"},
{334, "SELECTEDINDEX"},
{335, "SELECT"},
{336, "DESELECT"},
{337, "MAKEVISIBLE"},
{338, "COMBOBOX_USERITEM"},
{339, "USERTEXT"},
{340, "TLIST_NOSELECTION"},
{341, "TLIST_ACTIVATED"},
{342, "RB_AUTOTABSTOP"},
{343, "RB_AUTOCOORD"},
{344, "RADIOGROUP"},
{345, "LOCALSTATE"},
{346, "GROUPLEADER"},
{347, "STBAR_ADDLAMP"},
{348, "STBAR_MSGLINE"},
{349, "STBAR_LAMP_INDEX"},
{350, "STBAR_LAMP_VALUE"},
{351, "STBAR_SHOW_WORKING"},
{352, "STBAR_BLANK_MSG"},
{353, "STBAR_MAKE_CURRENT"},
{354, "MENU_MENUITEM"},
{355, "MENU_ACCEL_CODE"},
{356, "MENU_ACCELERATOR"},
{357, "MENU_ACCEL_MOD"},
{358, "MENU_HELP"},
{359, "MENU_HIDDEN"},
{360, "MENU_ICON"},
{361, "MENU_ID"},
{362, "MENU_STARTGROUP"},
{363, "MENU_STATE"},
{364, "MENU_SUBMENU"},
{365, "MENU_TEAROFF"},
{366, "MENU_ENDSUBMENU"},
{367, "MENU_MENUUPDATE"},
{368, "MENU_POPVIEW"},
{369, "MENU_POPXY"},
{370, "MENU_CREATEUI"},
{371, "MENU_FLAGS"},
{372, "MENU_NOSMARTBAR"},
{373, "TIMER_SCHEDULE"},
{374, "TIMER_EXPIRED"},
{375, "TIMER_CREATE"},
{376, "FONT_CHARSET"},
{377, "FONT_SIZE"},
{378, "FONT_STYLE"},
{379, "FONT_WEIGHT"},
{380, "FONT_WIDTH"},
{381, "FONT_IMAGETEXT"},
{382, "FONT_KERNING"},
{383, "FONT_NAME"},
{384, "IMAGE_URL"},
{385, "IMAGE_VALUE"},
{386, "SCALE"},
{387, "SCROLL_STYLE"},
{388, "IMAGE_SELECTRECT"},
{389, "IMAGE_SELECTALL"},
{390, "IMAGE_CUT"},
{391, "IMAGE_COPY"},
{392, "IMAGE_PASTE"},
{393, "IMAGE_QUALITY"},
{394, "IMAGE_DITHER"},
{395, "SCROLL_POSITION"},
{396, "VIEWPORT_SIZE"},
{397, "CLASSNAME"},
{398, "EVENT"},
{399, "EVENTNAME"},
{400, "EVENT_ARGNAME"},
{401, "EVENT_ARGVALUE"},
{402, "CUSTOM_PROPERTY"},
{403, "CUSTOM_PROPERTY_NAME"},
{404, "CUSTOM_PROPERTY_VALUE"},
{405, "CLIPBOARD_CLEAR"},
{406, "CLIPBOARD_GET"},
{407, "CLIPBOARD_SET"},
{408, "CLIPBOARD_LENGTH"},
{409, "POPUPHELP_STRING"},
{410, "POPUPHELP_ID"},
{411, "TAB_TOPPAGE"},
{412, "TAB_TABSIDE"},
{413, "TAB_PAGEINFO"},
{414, "TAB_PAGENUM"},
{415, "TAB_PAGEID"},
{416, "PROMPTLIST_CANVAS"},
{417, "PROMPTLIST_ADD_PROMPT_ID"},
{418, "PROMPTLIST_REMOVE_PROMPT"},
{419, "PROMPTLIST_UPDATE_PROMPT_ID"},
{420, "PROMPTLIST_DONE_PROMPT"},
{421, "PROMPT_ENUM"},
{422, "PROMPT_EOF"},
{423, "PROMPT_AOF"},
{424, "PROMPT_LINECOUNT"},
{425, "PROMPT_COLORNUM"},
{426, "PROMPT_FONTNUM"},
{427, "PROMPT_ITEMPOS"},
{428, "PROMPT_ITEMSIZE"},
{429, "PROMPT_BGPATTERN"},
{430, "LOGON_USERNAME_LABEL"},
{431, "LOGON_PASSWORD_LABEL"},
{432, "LOGON_DATABASE_LABEL"},
{433, "LOGON_USERNAME_VALUE"},
{434, "LOGON_PASSWORD_VALUE"},
{435, "LOGON_DATABASE_VALUE"},
{436, "LOGON_OLDPASSWORD_LABEL"},
{437, "LOGON_NEWPASSWORD_LABEL"},
{438, "LOGON_RETYPE_PASSWORD_LABEL"},
{439, "LOGON_OLDPASSWORD_VALUE"},
{440, "LOGON_NEWPASSWORD_VALUE"},
{441, "LOGON_RETYPE_PASSWORD_VALUE"},
{442, "DISPERR_SQL_LABEL"},
{443, "DISPERR_ERROR_LABEL"},
{444, "DISPERR_SQL_VALUE"},
{445, "DISPERR_ERROR_VALUE"},
{446, "LOV_ROWS"},
{447, "LOV_AUTOSIZECOLS"},
{448, "LOV_DEFINECOL"},
{449, "LOV_COLRALIGN"},
{450, "LOV_SELECTION"},
{451, "LOV_REQUEST_ROW"},
{452, "LOV_AUTOREDUCECHAR"},
{453, "LOV_UNREDUCE"},
{454, "LOV_FIND_VAL"},
{455, "LOV_LONGLIST_LABEL"},
{456, "LOV_NOTLONGLIST"},
{457, "EDITOR_BOTTOM_TITLE"},
{458, "EDITOR_VALUE_REQD"},
{459, "EDITOR_VSCROLL"},
{460, "EDITOR_SCROLLBAR"},
{461, "EDITOR_MULTILINE"},
{462, "EDITOR_PESRCHREP"},
{463, "EDITOR_PESRCHFOR"},
{464, "EDITOR_PEREPLWTH"},
{465, "EDITOR_PEREPLACE"},
{466, "EDITOR_PEREPALL"},
{467, "EDITOR_PECONTBEGIN"},
{468, "EDITOR_PECONTINUE"},
{469, "EDITOR_PEENDREG"},
{470, "EDITOR_PENOMATCH"},
{471, "PARAM_MUSTFILL"},
{472, "PARAM_REQUIRED"},
{473, "PARAM_NUM_DISPLAYED"},
{474, "PARAM_ERR_REQUIRED"},
{475, "PARAM_ERR_MUSTFILL"},
{476, "EVENT_ALERT_DISMISS"},
{477, "EVENT_MENU"},
{478, "EVENT_WINSYS"},
{479, "EVENT_SCROLL"},
{480, "EVENT_WINDOW_FINAL"},
{481, "EVENT_SHOWOPTIONS"},
{482, "EVENT_DELIMAGE"},
{483, "EVENT_USER_DEFINED"},
{484, "NODE_STATE"},
{485, "NUM_CHILD_NODES"},
{486, "NUM_SELECTED"},
{487, "SELECT_NODE"},
{488, "EVENT_SELECTED"},
{489, "EVENT_EXPANDED"},
{490, "EVENT_COLLAPSED"},
{491, "EVENT_ACTIVATED"},
{492, "EVENT_DESELECTED"},
{493, "REQUESTED_EVENTS"},
{494, "ADD_NODE"},
{495, "DELETE_NODE"},
{496, "MULTI_SELECT"},
{497, "SHOW_LINES"},
{498, "SHOW_SYMBOLS"},
{499, "ALTER_NODE"},
{500, "NODE_ID"},
{501, "TREE_FREEZE"},
{502, "NODE_ANCHOR"},
{503, "NODE_OFFSET"},
{504, "NODE_SELECTION"},
{505, "NODE_DATA"},
{506, "NUM_CHILDREN"},
{507, "RESET_CHILDREN"},
{508, "JHELP_TOPICID"},
{509, "HBOOK_TITLE"},
{510, "SERVER_USER_PARAMS"},
{511, "ALL_PROPERTIES"},
{512, "ALL_LISTENERS"},
{513, "ACCESSIBILITY_SUPPORT"},
{514, "ACCESSIBILITY_DESC"},
{515, "ACCESSIBILITY_NAME"},
{516, "KEYBINDING"},
{517, "HEARTBEAT"},
{518, "SB_NONBLOCKING"},
{519, "LATENCY_CHECK"},
{520, "DELETE_CHILDREN"},
{521, "FOCUSLIST_REMOVE"},
{522, "STBAR_REMOVE_LAMPS"},
{523, "CTIME_MESSAGE"},
{524, "CTIME_ENABLE"},
{525, "NLS_LANG"},
{526, "ASYNC_MESSAGE"},
{527, "UPDATE_ASYNC_PROCESSING"},
{528, "MAX_LENGTH_IS_BYTES"},
{529, "DATETIME_LOCAL_TZ"},
{530, "DEFAULT_LOCAL_TZ"},
{531, "OK_TO_PASTE"},
{532, "SELECTION_START"},
{533, "SELECTION_END"},
{534, "STOP_QUERY_LABEL"},
{535, "RT_MSE_PRSD_NODE"}};
static char* find_prop(match_id)
int match_id;
{
struct or_prop_id*guess;
struct or_prop_id* low = &or_prop_id[0];
struct or_prop_id* high =
            &or_prop_id[sizeof(or_prop_id)/sizeof(struct or_prop_id) - 1];

    while (low <= high)
    {
        guess = low + ((high - low) >> 1);
        if ( guess->id == match_id)
            return guess->name;
        else
        if ( guess->id < match_id)
            low = guess + 1;
        else
            high = guess - 1;
    }
    return "(unknown)";
}
/***********************************************************************
 * The following logic allows us to feed in the interesting ports.
 */
static int extend_listen_flag; /* Feed in extra listener ports            */ 
static int match_port[100];    /* List of ports to match against          */
static int match_cnt;            /* Number of ports in the list    */
static int ora_web_port[100];    /* List of ports to match against          */
static int ora_web_cnt;          /* Number of ports in the list    */
static int ora_tunnel_port[100]; /* List of ports to match against          */
static int ora_tunnel_cnt;       /* Number of ports in the list    */
static void web_match_add(arr, cnt, port)
int * arr;
int * cnt;
int port;
{
    if (*cnt < 100)
    {
       arr[*cnt] = port;
       (*cnt)++;
    }
    return;
}
/*
 * Deal with a fragment of ORACLE forms traffic
 */
void oraforms_verbose_dispose(ofp, sep)
FILE * ofp;
struct script_element * sep;
{
unsigned char * x;
unsigned char * top;

    x = sep->body;
    top = sep->body + sep->body_len;
    if (*(top - 2) == 0xf0 && *(top - 1) < 9)
    {
        while  (x < top)
            x = forms60_handle(ofp, x, top, 1);
    }
    return;
}
/*
 * Function that is called to process messages
 */
void do_ora_forms(sep)
struct script_element * sep;
{
    if (ap == NULL)
    {
        ap = (struct ora_web_sess *) calloc(sizeof(struct ora_web_sess),1);
        ap->pragma_cnt[0] = 0;
        ap->pragma_cnt[1] = 0;
        ap->f_enc_dec[0] = (unsigned char *) NULL;
        ap->f_enc_dec[1] = (unsigned char *) NULL;
        ap->gday = (unsigned char *) NULL;
    }
    do_http(sep);
    return;
}
/*
 * Dump out a human-readable rendition of the ORACLE forms messages
 * - Messages consist of:
 *   - An action code (top 4 bits of the first byte)
 *   - Optional header information (eg. Handler ID, cross-reference to target
 *     of delta message, class id)
 *   - An optional array of properties.
 * - Properties consist of:
 *   - A type code (top 4 bits of first property byte)
 *   - A Property index/code (next 12 bits)
 *   - Variable data that depends on the property. Properties can be numbers,
 *     strings, booleans, points, rectangles and even embedded messages.
 */
unsigned char * forms60_handle(ofp, base, top, out_flag)
FILE *ofp;
unsigned char * base;
unsigned char * top;
int out_flag;
{
unsigned char * x = base;
unsigned int action;
unsigned int delta;
unsigned int classid;
unsigned int handlerid;

    if (!out_flag)
        return top;
    if (*x == 0xf0)
    {
        if (x == (top - 2))
        {
            fprintf(ofp, "e2endmess|f0%02x\n",  *(x + 1));
            return top;
        }
        else
        {
            fputs("e2nullmess|f0\n", ofp);
            return x + 1;
        }
    }
    fputs("e2mess ", ofp);
    switch(*x & 0xf0)
    {
    case 0:
        action = 1;          /* Create */
        fputs("CREATE ", ofp);
        break;
    case 0x10:
        action = 2;          /* Update */
        fputs("UPDATE ", ofp);
        break;
    case 0x20:
        action = 3;          /* Destroy */
        fputs("DESTROY ", ofp);
        break;
    case 0x30:
        action = 4;          /* Get */
        fputs("GET ", ofp);
        break;
    case 0x40:
        action = 5;          /* Create Delta */
        fputs("CREATE_DELTA ", ofp);
        break;
    case 0x50:
        action = 6;          /* Update Delta */
        fputs("UPDATE_DELTA ", ofp);
        break;
    case 0x60:
        action = 7;          /* Client Get */
        fputs("CLIENT_GET ", ofp);
        break;
    case 0x70:
        action = 8;          /* Client Set */
        fputs("CLIENT_SET ", ofp);
        break;
    }
    classid = *x << 8;
    x++;
    if (action == 1 || action == 5)
    {
        classid |=  *x;
        x++;
    }
    classid = classid & (0x3ff);
    if (classid > 2000)
        classid += 3000;
    if (classid != 0)
        fprintf(ofp, "class %u (%s)", classid, find_name(classid));
    if (action == 5 || action == 6)
    {
        delta = *x++;
        fprintf(ofp, "delta %u ", delta);
    }
    if (*x & 0x80)
    {
        handlerid = ((*x & 0x7f) << 24) + (*(x + 1) << 16)
                  + (*(x + 2) << 8) + *(x + 3);
        x += 4;
    }
    else
    {
        handlerid = ((*x) << 8) + *(x + 1);
        x += 2;
    }
    fprintf(ofp, "handler %u", handlerid);
/*
 * Output the whole message header as it came in
 */
    fputc('|', ofp);
    (void) gen_handle(ofp, base, x, 1);
/*
 * Output the properties
 */
    while (x < top)
    {
        if (*x == 0xf0)
            return x + 1;
        x = forms60_property(ofp, x, top);
    }
    return x;
}
static unsigned char * outstr(ofp, base, top)
FILE *ofp;
unsigned char * base;
unsigned char * top;
{
unsigned char * x = base;
unsigned int len;

    if (top - base < 2)
        return top;
    len = ((*x) << 8) + *(x + 1);  /* For now, do not cater for the case
                                    * when the string isn't all there.  */
    x += 2;
    if (x + len >= top)
    {
        fprintf(ofp, "String length %u not all there|", len);
        (void) gen_handle(ofp, base, top, 1);
        return top;
    }
    fprintf(ofp, "string %u:%*.*s", len, len, len, x);
                       /* Ignore potential multibyte UTF characters */
    return x + len;
}
static unsigned char * forms60_property(ofp, base, top)
FILE *ofp;
unsigned char * base;
unsigned char * top;
{
unsigned char * x = base;
unsigned int propsuper;
unsigned int propid;
unsigned int len;
int mess_flag = 0;

        
    propsuper = (*x & 0xf0) >> 4;
    if (propsuper != 0xb && propsuper != 0xd)
    { 
        propid = ((*x << 8) + *(x + 1)) & 0xfff;
        fprintf(ofp, "  e2prop ID %u %s ", propid, find_prop(propid));
        x += 2;
    }
    switch(propsuper)
    {
    case 0:
        fprintf(ofp, "int %d", ((*x) << 24) + (*(x + 1) << 16) + (*(x + 2) << 8)
                                       + (*(x + 3)));
        x += 4;
        break;
    case 1:
        fputs("int 0", ofp);
        break;
    case 2:
        fprintf(ofp, "int %d", *x);
        x++;
        break;
    case 3:
        fprintf(ofp, "int %d", ((*x) << 8) + *(x + 1));
        x += 2;
        break;
    case 4:
        fprintf(ofp, "tag %u ", (*x));
        x++;
        x = outstr(ofp, x, top);
        break;
    case 5:
        fputs("true", ofp);
        break;
    case 6:
        fputs("false", ofp);
        break;
    case 7:
        fprintf(ofp, "byte %u", *x);
        x++;
        break;
    case 8:
        fputs("null", ofp);
        break;
    case 9:
        fprintf(ofp, "dict_str %u %u", *x, *(x + 1));
        x += 2;
        break;
    case 0xa:
        fprintf(ofp, "point (%u,%u)",
             ((*x) << 8) + *(x + 1), (*(x + 2) << 8) + *(x + 3));
        x += 4;
        break;
    case 0xb:
        fprintf(ofp, "   index_tag %u", (*x) & 0xf);
        x++;
        break;
    case 0xc:
        fprintf(ofp, "point (%u,%u)",*x, *(x + 1));
        x += 2;
        break;
    case 0xd:
        x++;
        fprintf(ofp, "   delete_mask %x", ((*x) << 8) + *(x + 1));
        x += 2;
        break;
    case 0xe:
        switch(*x)
        {
        case 2:           /* String array */
            x++;
            len =  ((*x) << 24) + (*(x + 1) << 16) + (*(x + 2) << 8)
                                       + (*(x + 3));
            x += 4;
            fprintf(ofp, "string_array %u {", len);
            if (len)
            {
                x = outstr(ofp, x, top);
                len--;
                while (len > 0 && x < top)
                {
                    fputc(',', ofp);      /* No escaping going on! */
                    x = outstr(ofp, x, top);
                    len--;
                }
            }
            fputc('}', ofp); 
            break;
        case 4:                   /* Character */
            x++;
            fprintf(ofp, "char %u", *x);
            x++;
            break;
        case 5:           /* Float */
            {
            float f;

                 x++;
                 memcpy((char *) &f, x, sizeof(float));
                 fprintf(ofp, "float %.23g", f);
                 x += sizeof(float);
            }
            break;
        case 6:           /* Date, in milliseconds: avoid potential problems
                           * with lack of support for 64 bit integers */
            x++;
            fprintf(ofp, "date %x%08x", 
                   ((*x) << 24) + (*(x + 1) << 16) + (*(x + 2) << 8)
                                       + (*(x + 3)),
                   (*(x + 4) << 24) + (*(x + 5) << 16) + (*(x + 6) << 8)
                                       + (*(x + 7)));
            x += 8;
            break;
        case 7:
        case 15:                /* Byte array */
            x++;
            len =  ((*x) << 24) + (*(x + 1) << 16) + (*(x + 2) << 8)
                                       + (*(x + 3));
            x += 4;
            fprintf(ofp, "byte_array %u {", len);
            if (len)
            {
                fprintf(ofp, "%x", *x);
                x++;
                len--;
                while (len > 0 && x < top)
                {
                    fputc(',', ofp); 
                    fprintf(ofp, "%x", *x);
                    x++;
                    len--;
                }
            }
            fputc('}', ofp); 
            break;
        case 8:                           /* Embedded message */
            fputc('|', ofp);
            (void) gen_handle(ofp, base, x, 1);
            fputs("---->", ofp);
            x = forms60_handle(ofp, x + 1, top, 1);
            fputs("<----\n", ofp);
            mess_flag = 1;
            break;
        case 10:                          /* Rectangle */
            x++;
            fprintf(ofp, "rectangle (%u,%u,+%u,+%u)", 
                   ((*x) << 8) + (*(x + 1)), (*(x + 2) << 8)
                                       + (*(x + 3)),
                   (*(x + 4) << 8) + (*(x + 5)) , (*(x + 6) << 8)
                                       + (*(x + 7)));
            x += 8;
            break;
        default:
            fprintf(ofp, "Extended Property Logic Error %u", *x);
            x++;
            break;
        }
        break;
    default:
        fprintf(ofp, "Property Logic Error %u", *x);
        x++;
        break;
    }
    if (!mess_flag)
    {
        fputc('|', ofp);
        (void) gen_handle(ofp, base, x, 1);
    }
    return x;
}
/*
 * Function that is called to process HTTP messages
 */
static void do_http(sep)
struct script_element * sep;
{
    if (sep->head[1] == 'D')
        http_client(sep);
    else
        http_server(sep);
    return;
}
/*
 * Process response from server to client
 */
static void http_server(sep)
struct script_element * sep;
{
unsigned char * x;
unsigned char * x1;
unsigned char * x2;
unsigned char * x3;
unsigned char * top;
unsigned char * b;
int i;

    x = sep->body;
    top = sep->body + sep->body_len;
    x1 = x;
    ap->plain_flag = 1;
    for (;;)
    {
/*
 * Process all the whole lines
 * -   x tracks the data that is processed.
 * -   x1 tracks the beginnings of lines
 * -   x2 tracks the ends of lines 
 * -   x3 is used for scanning within a delimited line
 *
 * Search for the end of a line. Expect carriage return/line feed.
 */
        for (x2 = x1; x2 < top; x2++)
            if ( *x2 == '\n' && (x2 > x1 && *(x2 - 1) == '\r' ))
                break;
        if (x2 >= top)
            return;               /* Run out!? */
        if (x2 == (x1 + 1))
        {
            x1 += 2;
            if (x1 >= top)
                return;
            break;                /* End of the HTTP header */
        }
        else
        if ( (x2 >= (x1 + 9))
         && ( !strncasecmp(x1, "Pragma: ", 8)))
        {
            i = atoi(x1 + 8);
            if (i <= ap->pragma_cnt[0]
              && (ap->gday != NULL
                  || ap->f_enc_dec[0] != NULL))
                    return;
            ap->pragma_cnt[0] = i;
        }
        else
        if (x2 >= (x1 + 38) && !strncasecmp(x1, "Content-Type: application/octet-stream", 38))
            ap->plain_flag = 0;
/*
 * Advance to the next line
 */
            x1 = x2 + 1;
    }
/*
 * Deal with the payload
 *
 * Initialise decryption?
 */
    if (ap->f_enc_dec[0] == (unsigned char *) NULL
      && ap->gday != (char *) NULL
      && !strncmp((char *) x1,"Mate",4))
    {
        ap->f_enc_dec[0] = ready_code( ap->gday, x1 + 4);
        ap->f_enc_dec[!0] = ready_code( ap->gday, x1 + 4);
        if (top > x1 + 8)
            block_enc_dec(ap->f_enc_dec[0],
                       x1 + 8, x1 + 8, (top - x1) - 8);
    }
    else
    if (ap->f_enc_dec[0] != (unsigned char *) NULL
      && (ap->plain_flag == 0))
        block_enc_dec(ap->f_enc_dec[0], x1, x1, top - x1);
    return;
}
/*
 * Process response from client to server
 */
static void http_client(sep)
struct script_element * sep;
{
unsigned char * x;
unsigned char * x1;
unsigned char * x2;
unsigned char * x3;
unsigned char * top;
unsigned char * b;
int i;
int prag_flag;

    x = sep->body;
    top = sep->body + sep->body_len;
    x1 = x;
    for (prag_flag = 0;;)
    {
/*
 * Process all the whole lines, skipping any continues.
 * -   x tracks the data that is processed.
 * -   x1 tracks the beginnings of lines
 * -   x2 tracks the ends of lines 
 * -   x3 is used for scanning within a delimited line
 *
 * Search for the end of a line. Expect carriage return/line feed.
 */
        for (x2 = x1; x2 < top; x2++)
            if ( *x2 == '\n' && (x2 > x1 && *(x2 - 1) == '\r' ))
                break;
        if (x2 >= top)
            return;
        if (x2 == (x1 + 1))
        {
            x1 += 2;
            if (x1 >= top)
                return;
            break;
        }
        else
        if ( (x2 >= (x1 + 9))
          && ( !strncasecmp(x1, "Pragma: ", 8)))
        {
            i = atoi(x1 + 8);
            if (i <= ap->pragma_cnt[1]
              && (ap->gday != NULL
              || ap->f_enc_dec[0] != NULL))
                return;
            ap->pragma_cnt[1] = i;
            prag_flag = 1;
        }
/*
 * Advance to the next line
 */
        x1 = x2 + 1;
    }
    if (!prag_flag)
        return;
/*
 * Deal with the payload
 */
    if (ap->f_enc_dec[1] == (unsigned char *) NULL
      && !strncmp(x1,"GDay",4))
    {
        ap->gday = (unsigned char *) malloc(4);
        memcpy( ap->gday, x1 + 4, 4);
    }
    else
    if (top - x1 >= 8
      && !strcmp(x1, "NULLPOST"))
        return;
    else
    if (ap->f_enc_dec[1] != (unsigned char *) NULL)
        block_enc_dec(ap->f_enc_dec[1],
                       x1, x1, top - x1);
    return;
}
