#include "unsfark.h"

#include <windows.h>
#include <tchar.h>

static HWND			MainWindow;

static const TCHAR	Extensions[] = {'s','f','A','r','k',' ','f','i','l','e','s',' ','(','*','.','s','f','a','r','k',')',0,
								'*','.','s','f','a','r','k',0,
								'A','l','l',' ','f','i','l','e','s',' ','(','*','.','*',')',0,
								'*','.','*',0,0};

static const TCHAR	Extensions2[] = {'s','f','2',' ','f','i','l','e','s',' ','(','*','.','s','f','2',')',0,
								'*','.','s','f','2',0,
								'A','l','l',' ','f','i','l','e','s',' ','(','*','.','*',')',0,
								'*','.','*',0,0};

static const TCHAR	ErrorStr[] = _T("sfArk Error");
static const TCHAR	WindowName[] = _T("sfArk Extractor");
static const TCHAR	Load[] = _T("sfArk file to convert to soundfont:");
static const TCHAR	Save[] = _T("Name of saved soundfont file:");

/***************** init_ofn() ******************
 * Initializes the OPENFILENAME struct.
 */

static void init_ofn(OPENFILENAME *ofn, TCHAR * buffer, const TCHAR * extension)
{
	// Clear out fields
	ZeroMemory(ofn, sizeof(OPENFILENAME));

	// Set size
	ofn->lStructSize = sizeof(OPENFILENAME);

	// Store passed buffer for filename
	ofn->lpstrFile = buffer;
	ofn->nMaxFile = MAX_PATH;

	// Set owner
	ofn->hwndOwner = MainWindow;

	// Set extensions
	ofn->lpstrDefExt = ofn->lpstrFilter = extension;
	while (*ofn->lpstrDefExt++);
	ofn->lpstrDefExt += 2;
}

/****************** getLoadName() *****************
 * Get the user's choice of filename to load,
 * and copies it to specified buffer.
 */

static TCHAR * getLoadName(TCHAR * buffer)
{
	OPENFILENAME ofn;

	// Init OPENFILENAME
	init_ofn(&ofn, buffer, &Extensions[0]);
	ofn.lpstrFile[0] = 0;

	// Set title
	ofn.lpstrTitle = &Load[0];

	// Set flags
	ofn.Flags = OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST|OFN_LONGNAMES;

	// Present the dialog and get user's selection
	return (GetOpenFileName(&ofn) ? buffer : 0);
}

/****************** getSaveName() *****************
 * Get the user's choice of filename to save,
 * and copies it to specified buffer.
 */

static TCHAR * getSaveName(TCHAR * buffer)
{
	OPENFILENAME ofn;

	// Init OPENFILENAME
	init_ofn(&ofn, buffer, &Extensions2[0]);

	// Set title
	ofn.lpstrTitle = &Save[0];

	// Set flags
	ofn.Flags = OFN_HIDEREADONLY|OFN_PATHMUSTEXIST|OFN_LONGNAMES;

	// Present the dialog and get user's selection
	return (GetOpenFileName(&ofn) ? buffer : 0);
}

int main(int argc, char * argv[])
{
	register char *	str;
	SFARKHANDLE		sfark;

	MainWindow = 0;

	if (!(sfark = SfarkAllocA()))
		printf("Not enough RAM!\r\n");
	else
	{
		int			errCode;

		errCode = 0;

		if (argc > 1)
			str = argv[1];
		else
			str = getLoadName(SfarkGetBuffer(sfark));

		if (str && !(errCode = SfarkOpen(sfark, str)))
		{
			if (argc > 2)
				str = argv[2];
			else
				str = getSaveName(SfarkGetBuffer(sfark));

			if (str && !(errCode = SfarkBeginExtract(sfark, str)))
			{
				unsigned char	dots;

				dots = 0;
				do
				{
					if (dots != SfarkPercent(sfark))
					{
						printf("*");
						++dots;
					}
				} while (!(errCode = SfarkExtract(sfark)));
				if (errCode > 0) errCode = 0;
				printf("\r\n");
			}
		}

		str = (char *)SfarkErrMsg(sfark, errCode);
		if (errCode)
			MessageBoxA(MainWindow, str, &ErrorStr[0], MB_OK|MB_ICONEXCLAMATION);
		else
			printf("\r\n%s\n", str);

		SfarkFree(sfark);
	}

 	return 0;
}
