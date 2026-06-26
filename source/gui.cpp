#include <windows.h>
#include <windowsx.h>

#include <string>
#include <vector>

#include "format.h"
#include "resource.h"
#include "types.h"

static HWND window = nullptr;
static HWND console = nullptr;

static std::vector<std::string> allActiveDrives;
static int driveLtrIdx = -1;
static bool forceFat32 = false;
static bool force32KiB = false;

static bool isDriveRemovable(std::string driveLabel) {
	std::string fullPath =  "\\\\.\\" + driveLabel + "\\";
	UINT driveType = GetDriveTypeA(fullPath.c_str());
    return driveType == DRIVE_REMOVABLE;
}

static void retrieveAllActiveDrives(void) {
    DWORD mask = GetLogicalDrives();
    // this should never happen, because C: will always exist
    if (mask == 0)
        return;

    for (int i = 0; i < 26; ++i) {
        if (mask & (1 << i)) {
            std::string driveLabel;
            driveLabel.push_back('A' + i);
            driveLabel.push_back(':');
            if(isDriveRemovable(driveLabel))
                allActiveDrives.push_back(driveLabel);
        }
    }
}

static void OpenConsoleWindow(void)
{
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    console = GetConsoleWindow();
}

static void CloseConsoleWindow(void)
{
    if (console != nullptr)
    {
        FreeConsole();
        PostMessage(console, WM_CLOSE, 0, 0);
    }
}

static void OnFormatButtonClick(HWND hwnd) {
    if(driveLtrIdx < 0)
    {
        MessageBox(hwnd, "You must select a drive first!", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    switch(MessageBox(hwnd, "This will ERASE ALL DATA on this drive! Press Yes to continue", "Warning", MB_YESNO | MB_ICONWARNING)) {
        case IDCANCEL:
        case IDNO:
            MessageBox(hwnd, "Format has been cancelled.", "Info", MB_OK | MB_ICONINFORMATION);
            return;
    }

    ArgFlags flags = {};
    flags.forceFat32 = forceFat32 ? 1 : 0;
    flags.force32KiB = force32KiB ? 1 : 0;

	char label[4 * 11 + 1] = {}; // Worst case 4 bytes per char.

    OpenConsoleWindow();

    int rc = formatSd(allActiveDrives.at(driveLtrIdx).c_str(), label, flags, 0);
    if (rc != 0)
    {
        std::string errormsg = "Format failed. Error " + std::to_string(rc);
        printf(errormsg.c_str());
        printf("\n");
        MessageBox(hwnd, errormsg.c_str(), "Error", MB_OK | MB_ICONERROR);
    }
    else
        MessageBox(hwnd, "Format successful.", "Success", MB_OK | MB_ICONINFORMATION);

    CloseConsoleWindow();
}

static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message) {
        case WM_INITDIALOG: {
            // Initialize drive list
            HWND comboBox = GetDlgItem(hwnd, IDC_DRIVELIST);
            retrieveAllActiveDrives();
            for(u32 i=0; i < allActiveDrives.size(); i++)
            {
                ComboBox_AddString(comboBox, (LONG_PTR)allActiveDrives.at(i).c_str());
            }

            // Initialize GUI icons
            SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON)));
            SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON)));

            return TRUE;
        }
        case WM_COMMAND: {
            if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDC_BUTTON_START) {
                OnFormatButtonClick(hwnd);
                break;
            }

            if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDC_CHECKBOX_FORCEFAT32) {
                forceFat32 = SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
                // 32KiB cluster size requires FAT32; enable or disable it accordingly
                HWND cb32KiB = GetDlgItem(hwnd, IDC_CHECKBOX_FORCE32KIB);
                EnableWindow(cb32KiB, forceFat32 ? TRUE : FALSE);
                if (!forceFat32) {
                    // reset 32KiB when FAT32 is unchecked
                    Button_SetCheck(cb32KiB, BST_UNCHECKED);
                    force32KiB = false;
                }
                break;
            }

            if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDC_CHECKBOX_FORCE32KIB) {
                force32KiB = SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
                break;
            }

            if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_DRIVELIST) {
                HWND comboBox = (HWND)lParam;
                int idx = SendMessage(comboBox, CB_GETCURSEL, 0, 0);
                driveLtrIdx = idx == CB_ERR ? driveLtrIdx : idx;
                break;
            }
            break;
        }
        case WM_CLOSE: {
            EndDialog(hwnd, LOWORD(wParam));
            break;
        }
    }
    return FALSE;
}

int guiMain(void) {
    FreeConsole();

    DialogBox(
        GetModuleHandle(NULL),    // Application instance
        "DIALOG_MAIN", // Dialog Resource ID
        window,                     // Parent window
        DialogProc              // Dialog Procedure
    );

    return 0;
}
