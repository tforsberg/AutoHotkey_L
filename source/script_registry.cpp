﻿///////////////////////////////////////////////////////////////////////////////
//
// AutoIt
//
// Copyright (C)1999-2007:
//		- Jonathan Bennett <jon@hiddensoft.com>
//		- Others listed at http://www.autoitscript.com/autoit3/docs/credits.htm
//      - Chris Mallett (support@autohotkey.com): adaptation of this file's
//        functions to interface with AutoHotkey.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
///////////////////////////////////////////////////////////////////////////////
//
// script_registry.cpp
//
// Contains registry handling routines.  Part of script.cpp
//
///////////////////////////////////////////////////////////////////////////////


// Includes
#include "stdafx.h" // pre-compiled headers
#include "script.h"
#include "util.h" // for strlcpy()
#include "globaldata.h"
#include "script_func_impl.h"


BIF_DECL(BIF_IniRead)
{
	_f_param_string_opt(aFilespec, 0);
	_f_param_string_opt(aSection, 1);
	_f_param_string_opt(aKey, 2);
	_f_param_string_opt(aDefault, 3);

	TCHAR	szFileTemp[_MAX_PATH+1];
	TCHAR	*szFilePart, *cp;
	TCHAR	szBuffer[65535] = _T("");					// Max ini file size is 65535 under 95
	TCHAR	szEmpty[] = _T("");
	// Get the fullpathname (ini functions need a full path):
	GetFullPathName(aFilespec, _MAX_PATH, szFileTemp, &szFilePart);
	if (*aKey)
	{
		// An access violation can occur if the following conditions are met:
		//	1) aFilespec specifies a Unicode file.
		//	2) aSection is a read-only string, either empty or containing only spaces.
		//
		// Debugging at the assembly level indicates that in this specific situation,
		// it tries to write a zero at the end of aSection (which is already null-
		// terminated).
		//
		// The second condition can ordinarily only be met if Section is omitted,
		// since in all other cases aSection is writable.  Although Section was a
		// required parameter prior to revision 57, empty or blank section names
		// are actually valid.  Simply passing an empty writable buffer appears
		// to work around the problem effectively:
		if (!*aSection)
			aSection = szEmpty;
		GetPrivateProfileString(aSection, aKey, aDefault, szBuffer, _countof(szBuffer), szFileTemp);
	}
	else if (*aSection
		? GetPrivateProfileSection(aSection, szBuffer, _countof(szBuffer), szFileTemp)
		: GetPrivateProfileSectionNames(szBuffer, _countof(szBuffer), szFileTemp))
	{
		// Convert null-terminated array of null-terminated strings to newline-delimited list.
		for (cp = szBuffer; ; ++cp)
			if (!*cp)
			{
				if (!cp[1])
					break;
				*cp = '\n';
			}
	}
	// If the value exists but is empty, the return value and GetLastError() will both be 0,
	// so assign ErrorLevel solely based on GetLastError():
	g_ErrorLevel->Assign(GetLastError() ? ERRORLEVEL_ERROR : ERRORLEVEL_NONE);
	// The above function is supposed to set szBuffer to be aDefault if it can't find the
	// file, section, or key.  In other words, it always changes the contents of szBuffer.
	_f_return(szBuffer); // Avoid using the length the API reported because it might be inaccurate if the data contains any binary zeroes, or if the data is double-terminated, etc.
}

#ifdef UNICODE
static BOOL IniEncodingFix(LPWSTR aFilespec, LPWSTR aSection)
{
	BOOL result = TRUE;
	if (!DoesFilePatternExist(aFilespec))
	{
		HANDLE hFile;
		DWORD dwWritten;

		// Create a Unicode file. (UTF-16LE BOM)
		hFile = CreateFile(aFilespec, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			DWORD cc = (DWORD)wcslen(aSection);
			DWORD cb = (cc + 1) * sizeof(WCHAR);
			
			aSection[cc] = ']'; // Temporarily replace the null-terminator.

			// Write a UTF-16LE BOM to identify this as a Unicode file.
			// Write [%aSection%] to prevent WritePrivateProfileString from inserting an empty line (for consistency and style).
			if (   !WriteFile(hFile, L"\xFEFF[", 4, &dwWritten, NULL) || dwWritten != 4
				|| !WriteFile(hFile, aSection, cb, &dwWritten, NULL) || dwWritten != cb   )
				result = FALSE;

			if (!CloseHandle(hFile))
				result = FALSE;

			aSection[cc] = '\0'; // Re-terminate.
		}
	}
	return result;
}
#endif

ResultType Line::IniWrite(LPTSTR aValue, LPTSTR aFilespec, LPTSTR aSection, LPTSTR aKey)
{
	TCHAR	szFileTemp[_MAX_PATH+1];
	TCHAR	*szFilePart;
	BOOL	result;
	// Get the fullpathname (INI functions need a full path) 
	GetFullPathName(aFilespec, _MAX_PATH, szFileTemp, &szFilePart);
#ifdef UNICODE
	// WritePrivateProfileStringW() always creates INIs using the system codepage.
	// IniEncodingFix() checks if the file exists and if it doesn't then it creates
	// an empty file with a UTF-16LE BOM.
	result = IniEncodingFix(szFileTemp, aSection);
	if(result){
#endif
		if (*aKey)
		{
			result = WritePrivateProfileString(aSection, aKey, aValue, szFileTemp);  // Returns zero on failure.
		}
		else
		{
			size_t value_len = ArgLength(1);
			TCHAR c, *cp, *szBuffer = talloca(value_len + 2); // +2 for double null-terminator.
			// Convert newline-delimited list to null-terminated array of null-terminated strings.
			for (cp = szBuffer; *aValue; ++cp, ++aValue)
			{
				c = *aValue;
				*cp = (c == '\n') ? '\0' : c;
			}
			*cp = '\0', cp[1] = '\0'; // Double null-terminator.
			result = WritePrivateProfileSection(aSection, szBuffer, szFileTemp);
		}
		WritePrivateProfileString(NULL, NULL, NULL, szFileTemp);	// Flush
#ifdef UNICODE
	}
#endif
	return SetErrorLevelOrThrowBool(!result);
}



ResultType Line::IniDelete(LPTSTR aFilespec, LPTSTR aSection, LPTSTR aKey)
// Note that aKey can be NULL, in which case the entire section will be deleted.
{
	TCHAR	szFileTemp[_MAX_PATH+1];
	TCHAR	*szFilePart;
	// Get the fullpathname (ini functions need a full path) 
	GetFullPathName(aFilespec, _MAX_PATH, szFileTemp, &szFilePart);
	BOOL result = WritePrivateProfileString(aSection, aKey, NULL, szFileTemp);  // Returns zero on failure.
	WritePrivateProfileString(NULL, NULL, NULL, szFileTemp);	// Flush
	return SetErrorLevelOrThrowBool(!result);
}



ResultType Line::RegRead(HKEY aRootKey, LPTSTR aRegSubkey, LPTSTR aValueName)
{
	Var &output_var = *OUTPUT_VAR;
	output_var.Assign(); // Init.  Tell it not to free the memory by not calling without parameters.

	HKEY	hRegKey;
	DWORD	dwRes, dwBuf, dwType;
	LONG    result;

	if (!aRootKey)
		return LineError(ERR_PARAM2_INVALID, FAIL, ARG2);

	// Open the registry key
	result = RegOpenKeyEx(aRootKey, aRegSubkey, 0, KEY_READ | g->RegView, &hRegKey);
	if (result != ERROR_SUCCESS)
		goto finish;

	// Read the value and determine the type.  If aValueName is the empty string, the key's default value is used.
	result = RegQueryValueEx(hRegKey, aValueName, NULL, &dwType, NULL, NULL);
	if (result != ERROR_SUCCESS)
	{
		RegCloseKey(hRegKey);
		goto finish;
	}

	LPTSTR contents, cp;

	// The way we read is different depending on the type of the key
	switch (dwType)
	{
		case REG_DWORD:
			dwRes = sizeof(dwBuf);
			result = RegQueryValueEx(hRegKey, aValueName, NULL, NULL, (LPBYTE)&dwBuf, &dwRes);
			if (result == ERROR_SUCCESS)
				output_var.Assign((DWORD)dwBuf);
			RegCloseKey(hRegKey);
			break;

		// Note: The contents of any of these types can be >64K on NT/2k/XP+ (though that is probably rare):
		case REG_SZ:
		case REG_EXPAND_SZ:
		case REG_MULTI_SZ:
		{
			dwRes = 0; // Retained for backward compatibility because values >64K may cause it to fail on Win95 (unverified, and MSDN implies its value should be ignored for the following call).
			// MSDN: If lpData is NULL, and lpcbData is non-NULL, the function returns ERROR_SUCCESS and stores
			// the size of the data, in bytes, in the variable pointed to by lpcbData.
			result = RegQueryValueEx(hRegKey, aValueName, NULL, NULL, NULL, &dwRes); // Find how large the value is.
			if (result != ERROR_SUCCESS || !dwRes) // Can't find size (realistically might never happen), or size is zero.
			{
				RegCloseKey(hRegKey);
				// Above realistically probably never happens; if it does and result != ERROR_SUCCESS,
				// setting ErrorLevel to indicate the error seems more useful than maintaining backward-
				// compatibility by faking success.
				break;
			}
			DWORD dwCharLen = dwRes / sizeof(TCHAR);
			// Set up the variable to receive the contents, enlarging it if necessary:
			// Since dwRes includes the space for the zero terminator (if the MSDN docs
			// are accurate), this will enlarge it to be 1 byte larger than we need,
			// which leaves room for the final newline character to be inserted after
			// the last item.  But add 2 to the requested capacity in case the data isn't
			// terminated in the registry, which allows double-NULL to be put in for REG_MULTI_SZ later.
			if (output_var.AssignString(NULL, (VarSizeType)(dwCharLen + 2)) != OK)
			{
				RegCloseKey(hRegKey);
				return FAIL; // FAIL is only returned when the error is a critical one such as this one.
			}

			contents = output_var.Contents(); // This target buf should now be large enough for the result.

			result = RegQueryValueEx(hRegKey, aValueName, NULL, NULL, (LPBYTE)contents, &dwRes);
			RegCloseKey(hRegKey);

			if (result != ERROR_SUCCESS || !dwRes) // Relies on short-circuit boolean order.
			{
				*contents = '\0'; // MSDN says the contents of the buffer is undefined after the call in some cases, so reset it.
				// Above realistically probably never happens; if it does and result != ERROR_SUCCESS,
				// setting ErrorLevel to indicate the error seems more useful than maintaining backward-
				// compatibility by faking success.
			}
			else
			{
				dwCharLen = dwRes / sizeof(TCHAR);
				// See ReadRegString() for more comments about the following:
				// The MSDN docs state that we should ensure that the buffer is NULL-terminated ourselves:
				// "If the data has the REG_SZ, REG_MULTI_SZ or REG_EXPAND_SZ type, then lpcbData will also
				// include the size of the terminating null character or characters ... If the data has the
				// REG_SZ, REG_MULTI_SZ or REG_EXPAND_SZ type, the string may not have been stored with the
				// proper null-terminating characters. Applications should ensure that the string is properly
				// terminated before using it, otherwise, the application may fail by overwriting a buffer."
				//
				// Double-terminate so that the loop can find out the true end of the buffer.
				// The MSDN docs cited above are a little unclear.  The most likely interpretation is that
				// dwRes contains the true size retrieved.  For example, if dwRes is 1, the first char
				// in the buffer is either a NULL or an actual non-NULL character that was originally
				// stored in the registry incorrectly (i.e. without a terminator).  In either case, do
				// not change the first character, just leave it as is and add a NULL at the 2nd and
				// 3rd character positions to ensure that it is double terminated in every case:
				contents[dwCharLen] = contents[dwCharLen + 1] = '\0';

				if (dwType == REG_MULTI_SZ) // Convert NULL-delimiters into newline delimiters.
				{
					for (cp = contents;; ++cp)
					{
						if (!*cp)
						{
							// Unlike AutoIt3, it seems best to have a newline character after the
							// last item in the list also.  It usually makes parsing easier:
							*cp = '\n';	// Convert to \n for later storage in the user's variable.
							if (!*(cp + 1)) // Buffer is double terminated, so this is safe.
								// Double null terminator marks the end of the used portion of the buffer.
								break;
						}
					}
					// else the buffer is empty (see above notes for explanation).  So don't put any newlines
					// into it at all, since each newline should correspond to an item in the buffer.
				}
			}
			output_var.SetCharLength((VarSizeType)_tcslen(contents)); // Due to conservative buffer sizes above, length is probably too large by 3. So update to reflect the true length.
			if (!output_var.Close()) // Must be called after Assign(NULL, ...) or when Contents() has been altered because it updates the variable's attributes and properly handles VAR_CLIPBOARD.
				return FAIL;
			break;
		}
		case REG_BINARY:
		{
			// See case REG_SZ for comments.
			dwRes = 0;
			result = RegQueryValueEx(hRegKey, aValueName, NULL, NULL, NULL, &dwRes); // Find how large the value is.
			if (result != ERROR_SUCCESS || !dwRes) // Can't find size (realistically might never happen), or size is zero.
			{
				RegCloseKey(hRegKey);
				break;
			}

			// Set up the variable to receive the contents, enlarging it if necessary.
			// AutoIt3: Each byte will turned into 2 digits, plus a final null:
			if (output_var.AssignString(NULL, (VarSizeType)(dwRes * 2)) != OK)
			{
				RegCloseKey(hRegKey);
				return FAIL;
			}
			contents = output_var.Contents();
			*contents = '\0';
			
			// Read the binary data into the variable, placed so that the last byte of
			// binary data will be overwritten as the hexadecimal conversion completes.
			LPBYTE pRegBuffer = (LPBYTE)(contents + dwRes * 2) - dwRes;
			result = RegQueryValueEx(hRegKey, aValueName, NULL, NULL, pRegBuffer, &dwRes);
			RegCloseKey(hRegKey);

			if (result != ERROR_SUCCESS)
				break;

			int j = 0;
			DWORD i, n; // i and n must be unsigned to work
			TCHAR szHexData[] = _T("0123456789ABCDEF");  // Access to local vars might be faster than static ones.
			for (i = 0; i < dwRes; ++i)
			{
				n = pRegBuffer[i];				// Get the value and convert to 2 digit hex
				contents[j + 1] = szHexData[n % 16];
				n /= 16;
				contents[j] = szHexData[n % 16];
				j += 2;
			}
			contents[j] = '\0'; // Terminate
			if (!output_var.Close()) // Length() was already set by the earlier call to Assign().
				return FAIL;
			break;
		}
		default:
			RegCloseKey(hRegKey);
			result = ERROR_UNSUPPORTED_TYPE; // Indicate the error.
			break;
	}

finish:
	return SetErrorsOrThrow(result != ERROR_SUCCESS, result);
} // RegRead()



ResultType Line::RegWrite(DWORD aValueType, HKEY aRootKey, LPTSTR aRegSubkey, LPTSTR aValueName, LPTSTR aValue)
// If aValueName is the empty string, the key's default value is used.
{
	HKEY	hRegKey;
	DWORD	dwRes, dwBuf;

	TCHAR *buf;
	#define SET_REG_BUF buf = aValue;
	
	LONG result;

	if (aValueType == REG_NONE || aValueType == REG_SUBKEY) // Can't write to these.
		return LineError(ERR_PARAM1_INVALID, FAIL, ARG1);
	if (!aRootKey)
		return LineError(ERR_PARAM2_INVALID, FAIL, ARG2);

	// Open/Create the registry key
	// The following works even on root keys (i.e. blank subkey), although values can't be created/written to
	// HKCU's root level, perhaps because it's an alias for a subkey inside HKEY_USERS.  Even when RegOpenKeyEx()
	// is used on HKCU (which is probably redundant since it's a pre-opened key?), the API can't create values
	// there even though RegEdit can.
	result = RegCreateKeyEx(aRootKey, aRegSubkey, 0, _T(""), REG_OPTION_NON_VOLATILE, KEY_WRITE | g->RegView, NULL, &hRegKey, &dwRes);
	if (result != ERROR_SUCCESS)
		goto finish;

	// Write the registry differently depending on type of variable we are writing
	switch (aValueType)
	{
	case REG_SZ:
		SET_REG_BUF
		result = RegSetValueEx(hRegKey, aValueName, 0, REG_SZ, (CONST BYTE *)buf, (DWORD)(_tcslen(buf)+1) * sizeof(TCHAR));
		break;

	case REG_EXPAND_SZ:
		SET_REG_BUF
		result = RegSetValueEx(hRegKey, aValueName, 0, REG_EXPAND_SZ, (CONST BYTE *)buf, (DWORD)(_tcslen(buf)+1) * sizeof(TCHAR));
		break;
	
	case REG_MULTI_SZ:
	{
		size_t length = _tcslen(aValue);
		// Allocate some temporary memory because aValue might not be a writable string,
		// and we would need to write to it to temporarily change the newline delimiters into
		// zero-delimiters.  Even if we were to require callers to give us a modifiable string,
		// its capacity may be 1 byte too small to handle the double termination that's needed
		// (i.e. if the last item in the list happens to not end in a newline):
		buf = tmalloc(length + 2);
		if (!buf)
		{
			result = ERROR_OUTOFMEMORY;
			break;
		}
		tcslcpy(buf, aValue, length + 1);
		// Double-terminate:
		buf[length + 1] = '\0';

		// Remove any final newline the user may have provided since we don't want the length
		// to include it when calling RegSetValueEx() -- it would be too large by 1:
		if (length > 0 && buf[length - 1] == '\n')
			buf[--length] = '\0';

		// Replace the script's delimiter char with the zero-delimiter needed by RegSetValueEx():
		for (LPTSTR cp = buf; *cp; ++cp)
			if (*cp == '\n')
				*cp = '\0';

		result = RegSetValueEx(hRegKey, aValueName, 0, REG_MULTI_SZ, (CONST BYTE *)buf
								, (DWORD)(length ? length + 2 : 0) * sizeof(TCHAR));
		free(buf);
		break;
	}

	case REG_DWORD:
		if (*aValue)
			dwBuf = ATOU(aValue);  // Changed to ATOU() for v1.0.24 so that hex values are supported.
		else // Default to 0 when blank.
			dwBuf = 0;
		result = RegSetValueEx(hRegKey, aValueName, 0, REG_DWORD, (CONST BYTE *)&dwBuf, sizeof(dwBuf));
		break;

	case REG_BINARY:
	{
		int nLen = (int)_tcslen(aValue);

		// String length must be a multiple of 2 
		if (nLen % 2)
		{
			result = ERROR_INVALID_PARAMETER;
			break;
		}

		int nBytes = nLen / 2;
		LPBYTE pRegBuffer = (LPBYTE) malloc(nBytes);
		if (!pRegBuffer)
		{
			result = ERROR_OUTOFMEMORY;
			break;
		}

		// Really crappy hex conversion
		int j = 0, i = 0, nVal, nMult;
		while (i < nLen && j < nBytes)
		{
			nVal = 0;
			for (nMult = 16; nMult >= 0; nMult = nMult - 15)
			{
				if (aValue[i] >= '0' && aValue[i] <= '9')
					nVal += (aValue[i] - '0') * nMult;
				else if (aValue[i] >= 'A' && aValue[i] <= 'F')
					nVal += (((aValue[i] - 'A'))+10) * nMult;
				else if (aValue[i] >= 'a' && aValue[i] <= 'f')
					nVal += (((aValue[i] - 'a'))+10) * nMult;
				else
				{
					free(pRegBuffer);
					RegCloseKey(hRegKey);
					result = ERROR_INVALID_PARAMETER;
					goto finish;
				}
				++i;
			}
			pRegBuffer[j++] = (char)nVal;
		}

		result = RegSetValueEx(hRegKey, aValueName, 0, REG_BINARY, pRegBuffer, (DWORD)j);
		free(pRegBuffer);
		break;
	}

	default:
		result = ERROR_INVALID_PARAMETER; // Anything other than ERROR_SUCCESS.
		break;
	} // switch()

	RegCloseKey(hRegKey);
	// Additionally, fall through to below:

finish:
	return SetErrorsOrThrow(result != ERROR_SUCCESS, result);
} // RegWrite()



LONG Line::RegRemoveSubkeys(HKEY hRegKey)
{
	// Removes all subkeys to the given key.  Will not touch the given key.
	TCHAR Name[256];
	DWORD dwNameSize;
	FILETIME ftLastWrite;
	HKEY hSubKey;
	LONG result;

	for (;;) 
	{ // infinite loop 
		dwNameSize = _countof(Name)-1;
		if (RegEnumKeyEx(hRegKey, 0, Name, &dwNameSize, NULL, NULL, NULL, &ftLastWrite) == ERROR_NO_MORE_ITEMS)
			return ERROR_SUCCESS;
		result = RegOpenKeyEx(hRegKey, Name, 0, KEY_READ | g->RegView, &hSubKey);
		if (result != ERROR_SUCCESS)
			break;
		result = RegRemoveSubkeys(hSubKey);
		RegCloseKey(hSubKey);
		if (result != ERROR_SUCCESS)
			break;
		result = RegDeleteKey(hRegKey, Name);
		if (result != ERROR_SUCCESS)
			break;
	}
	return result;
}



ResultType Line::RegDelete(HKEY aRootKey, LPTSTR aRegSubkey, LPTSTR aValueName)
{
	LONG result;

	if (!aRootKey)
		return LineError(ERR_PARAM1_INVALID, FAIL, ARG1);
	// Fix for v1.0.48: Don't remove the entire key if it's a root key!  According to MSDN,
	// the root key would be opened by RegOpenKeyEx() further below whenever aRegSubkey is NULL
	// or an empty string. aValueName is also checked to preserve the ability to delete a value
	// that exists directly under a root key.
	if (  (!aRegSubkey || !*aRegSubkey) && !aValueName  )
		return LineError(_T("Cannot delete root key"));

	// Open the key we want
	HKEY hRegKey;
	result = RegOpenKeyEx(aRootKey, aRegSubkey, 0, KEY_READ | KEY_WRITE | g->RegView, &hRegKey);
	if (result != ERROR_SUCCESS)
		goto finish;

	if (!aValueName) // Caller's signal to delete the entire subkey.
	{
		// Remove the entire Key
		result = RegRemoveSubkeys(hRegKey); // Delete any subitems within the key.
		RegCloseKey(hRegKey); // Close parent key.  Not sure if this needs to be done only after the above.
		if (result == ERROR_SUCCESS)
		{
			typedef LONG (WINAPI * PFN_RegDeleteKeyEx)(HKEY hKey , LPCTSTR lpSubKey , REGSAM samDesired , DWORD Reserved );
			static PFN_RegDeleteKeyEx _RegDeleteKeyEx = (PFN_RegDeleteKeyEx)GetProcAddress(GetModuleHandle(_T("advapi32")), "RegDeleteKeyEx" WINAPI_SUFFIX);
			if (g->RegView && _RegDeleteKeyEx)
				result = _RegDeleteKeyEx(aRootKey, aRegSubkey, g->RegView, 0);
			else
				result = RegDeleteKey(aRootKey, aRegSubkey);
		}
	}
	else
	{
		// Remove the value.
		result = RegDeleteValue(hRegKey, aValueName);
		RegCloseKey(hRegKey);
	}

finish:
	return SetErrorsOrThrow(result != ERROR_SUCCESS, result);
} // RegDelete()
