/*
Save this file as unsfark.c and compile it using this command

gcc -Wall -g -mfpmath=387 -o unsfark unsfark.c `pkg-config --cflags --libs gtk+-3.0` -lpthread -export-dynamic

Requires libgtk-dev package.

Then execute it using:
  ./unsfark
*/

#ifdef WIN32
#define HANDLE_T HANDLE
#else
#define HANDLE_T int

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#define _T(x) (x)
#define lstrlenA(x) strlen(x)
#define lstrcpyA(x,y) strcpy(x,y)
#define GlobalAlloc(x,y) malloc(y)
#define GlobalFree(y) free(y)
#define ZeroMemory(x,y) memset(x, 0, y)
#define CopyMemory(x,y,z) memcpy(x, y, z)
#define MoveMemory(x,y,z) memmove(x, y, z)
#define CloseHandle(fh) close(fh)
#define INVALID_SET_FILE_POINTER -1
#define FILE_CURRENT SEEK_CUR
#define SetFilePointer(handle, offset, distance, method) lseek(handle, offset, method)
#define DeleteFileA(buffer) unlink((char *)buffer)

typedef char TCHAR;
#endif

#include <zlib.h>
#include "unsfark.h"

#ifdef _MSC_VER
#if (_MSC_VER < 1300)
   typedef signed char       int8_t;
   typedef signed short      int16_t;
   typedef signed int        int32_t;
   typedef unsigned char     uint8_t;
   typedef unsigned short    uint16_t;
   typedef unsigned int      uint32_t;
#else
   typedef signed __int8     int8_t;
   typedef signed __int16    int16_t;
   typedef signed __int32    int32_t;
   typedef unsigned __int8   uint8_t;
   typedef unsigned __int16  uint16_t;
   typedef unsigned __int32  uint32_t;
#endif
#else
#include <stdint.h>
#endif

struct SFARKINFO_LPC {
	float			AcHist[129*4];
	int16_t			History[256];
	int16_t			DecodeHist[128];
};


struct SFARKINFO_V2 {
	short			BitFramePackMethod;
	union {

		z_stream				Zip;
		struct SFARKINFO_LPC	Lpc;
	} u;
	int16_t			PrevDataShiftVal[20];
	uint16_t		PrevUnpack2EncodeCnt;
	uint16_t		PrevUnpack3EncodeCnt;
	uint16_t		CalcShiftEncodeCnt;
	uint16_t		ShiftEncodeCnt;
	uint16_t		NumShortsInLpcBlock;	// 4096 or 1024
	uint16_t		NumShortsInLpcFrame;	// 128 or 8
	uint8_t			Unpack2EncodeLimit;
	uint8_t			Unpack3EncodeLimit;
	unsigned char	LpcCurrHistNum;
};

#pragma pack (1)
struct PACKITEM {
	unsigned char		EncodeCnt;
	unsigned char		BitShiftAmt;
	union {
		unsigned short		Data1;
		struct PACKITEM *	NextItemArray;
	} u;
};
#pragma pack ()

struct SFARKINFO_V1 {
	HANDLE_T		PartFileHandle;
	unsigned char *		OutbufPtr;
	unsigned char *		DecompBuf;
	const char *		AppFontName;
	struct PACKITEM *	PackItemArray;
	struct PACKITEM *	CurPackItemArray;
	unsigned int		VarDecodeDWord;
	unsigned int		VarDecodeByteCnt;
	unsigned int		EncodeCnt;
	unsigned short		PrevBytesInDecompBuf;
	unsigned short		BytesInDecompBuf;
	unsigned int		FirstEncodeCnt;
	unsigned int		SecondEncodeCnt;
	unsigned char		PartNum;
	unsigned char		BlockFlags;
};

struct SFARKINFO {
	unsigned char *	WorkBuffer1;
	unsigned char *	WorkBuffer2;
	HANDLE_T		InputFileHandle;
	HANDLE_T		OutputFileHandle;
	// ==========================
	uint32_t		FileUncompSize;			// Read from sfArk file header
	int32_t			FileChksum;				// Read from sfArk file header
	uint32_t		LeadingPadUncompSize;	// Read from sfArk file header
	uint32_t		BitPackEndOffset;		// Read from sfArk file header
	// ==========================
	uint32_t		RunningUncompSize;
	int32_t			RunningChksum;
	uint32_t		NumBitsInRegister;
	uint32_t		BitRegister;
	unsigned short	InputBufIndex;
	unsigned short	InputBuf[2048];
	unsigned char	CompressType;			// Read from sfArk file header
	unsigned char	RunState;
	unsigned char	Percent;
	unsigned char	Flags;
	union {
	struct SFARKINFO_V2	v2;
	struct SFARKINFO_V1	v1;
	} u;
};

// SFARKINFO->RunState
#define SFARK_DONE			0
#define SFARK_BITPACK		2
#define SFARK_LZIP			3


// SFARKINFO->Flags
#define SFARK_OUT_OPEN		1
#define SFARK_IN1_OPEN		2
#define SFARK_IN2_OPEN		4
#define SFARK_TEMP_MADE		8
#define SFARK_UNICODE		0x10




















static const char * SfArkId = ".sfArk";

static const char ErrorMsgs[] = "Success\0\
Program error\0\
Need more RAM\0\
Corrupt header\0\
ID not found. File is corrupt or not a compressed soundfont\0\
Header checksum fail\0\
Encryted file\0\
Unknown compression\0\
Bad compressed data\0\
Bad bit-packed block length\0\
Bad bit unpacking\0\
Bad encode count\0\
Bad shift position\0\
Data checksum isn't correct\0\
Can't save data to soundfont\0\
Can't open compressed file\0\
Can't create soundfont file\0\
Can't set file position\0\
Can't read from compressed file\0";

static const char * UnknownErr = "Unknown error";




#define SFARKERR_OK				0
#define SFARKERR_APP			-1
#define SFARKERR_MEM			-2
#define SFARKERR_BADHDR			-3
#define SFARKERR_NOID			-4
#define SFARKERR_BADHDRSUM		-5
#define SFARKERR_OLDVERS		-6
#define SFARKERR_UNKCOMP		-7
#define SFARKERR_BADCOMP		-8
#define SFARKERR_BADLEN			-9
#define SFARKERR_BADUNPACK		-10
#define SFARKERR_BADCNT			-11
#define SFARKERR_BADSHIFTCNT	-12
#define SFARKERR_CHKSUM			-13
#define SFARKERR_SAVE			-14
#define SFARKERR_SFARKOPEN		-15
#define SFARKERR_SFOPEN			-16
#define SFARKERR_POS			-17
#define SFARKERR_SFARKREAD		-18































// ===============================================================
// File I/O
// ===============================================================

static void closeFile(HANDLE_T fh)
{
	CloseHandle(fh);
}





/****************** openSfarkFile() *****************
 * Opens the sfArk file to load.
 *
 * RETURN: 0 = success, or negative error number.
 */

static int openSfarkFile(struct SFARKINFO * sfarkInfo, const void * name)
{
#ifdef WIN32
	if ((sfarkInfo->Flags & SFARK_UNICODE) && name != sfarkInfo->WorkBuffer2)
		sfarkInfo->InputFileHandle = CreateFileW((WCHAR *)name, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	else
		sfarkInfo->InputFileHandle = CreateFileA((char *)name, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if (sfarkInfo->InputFileHandle == (HANDLE)INVALID_HANDLE_VALUE)
#else
	if ((sfarkInfo->InputFileHandle = open((char *)name, O_RDONLY, S_IRUSR)) == -1)
#endif
		return SFARKERR_SFARKOPEN;

	return 0;
}





/****************** createSfFile() *****************
 * Opens the soundfont file to save.
 *
 * RETURN: 0 = success, or negative error number.
 */

static int createSfFile(struct SFARKINFO * sfarkInfo, const void * name)
{
#ifdef WIN32
	if ((sfarkInfo->Flags & SFARK_UNICODE) && name != sfarkInfo->WorkBuffer2)
		sfarkInfo->OutputFileHandle = CreateFileW((WCHAR *)name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_RANDOM_ACCESS, 0);
	else
		sfarkInfo->OutputFileHandle = CreateFileA((char *)name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_RANDOM_ACCESS, 0);
	if (sfarkInfo->OutputFileHandle == (HANDLE)INVALID_HANDLE_VALUE)
#else
	if ((sfarkInfo->OutputFileHandle = open((char *)name, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1)
#endif
		return SFARKERR_SFOPEN;

	return 0;
}





/****************** setSfarkFilePos() *****************
 * Sets the sfArk file position relative to current
 * position.
 *
 * RETURN: 0 = success, or negative error number.
 */

static int setSfarkFilePos(struct SFARKINFO * sfarkInfo, long offset)
{
	if (SetFilePointer(sfarkInfo->InputFileHandle, offset, 0, FILE_CURRENT) == INVALID_SET_FILE_POINTER)
		return SFARKERR_POS;

	return 0;
}





/****************** readSfarkFile() *****************
 * Reads the specified number of bytes from the sfArk
 * file, and puts them in the supplied buffer.
 *
 * RETURN: The number of bytes read if success (ie, may
 * read less bytes than requested if the end of file),
 * or negative error number.
 */

static int readSfarkFile(struct SFARKINFO * sfarkInfo, void * ptr, unsigned int count)
{
#ifdef WIN32
	DWORD	result;

	if (!ReadFile(sfarkInfo->InputFileHandle, ptr, count, &result, 0))
#else
	int	result;

	if ((result = read(sfarkInfo->InputFileHandle, ptr, count)) == -1)
#endif
		return SFARKERR_SFARKREAD;

	return (int)result;
}





/****************** writeToFontFile() *****************
 * Writes the specified number of bytes from the
 * supplied buffer to the soundfont file.
 *
 * RETURN: 0 = success, or negative error number.
 */

static int writeToFontFile(struct SFARKINFO * sfarkInfo, const void * ptr, unsigned int count)
{
#ifdef WIN32
	DWORD	result;

	if (!WriteFile(sfarkInfo->OutputFileHandle, ptr, count, &result, 0) || result != count)
#else
	if (write(sfarkInfo->OutputFileHandle, ptr, count) != count)
#endif
		return SFARKERR_SAVE;

	return 0;
}















































// ===============================================================
// LPC decoder
// ===============================================================

static void schur(struct SFARKINFO * sfarkInfo, unsigned int dataSize, int32_t * dest)
{
	float		sum;

	sum = sfarkInfo->u.v2.u.Lpc.AcHist[0] + sfarkInfo->u.v2.u.Lpc.AcHist[129] + sfarkInfo->u.v2.u.Lpc.AcHist[(129*2)] + sfarkInfo->u.v2.u.Lpc.AcHist[(129*3)];;
	if (sum == 0.0)
	    ZeroMemory(dest, dataSize * sizeof(uint32_t));
	else
	{
		unsigned int	i, j;
		float			floatArray[128];
		float			floatArray2[128];

		for (i = 1; i < dataSize + 1; ++i) floatArray[i - 1] = floatArray2[i - 1] = sfarkInfo->u.v2.u.Lpc.AcHist[i] + sfarkInfo->u.v2.u.Lpc.AcHist[129+i] + sfarkInfo->u.v2.u.Lpc.AcHist[(129*2)+i] + sfarkInfo->u.v2.u.Lpc.AcHist[(129*3)+i];

		j = 0;
		for (;;)
		{
			float		sum2;

			sum2 = -floatArray[0] / sum;
			sum = floatArray[0] * sum2 + sum;
			dest[j] = (int32_t)(sum2 * 16384.0);
			if (++j >= dataSize) break;
			for (i = 0; i < dataSize - j; ++i)
			{
		        floatArray[i] = (sum2 * floatArray2[i]) + floatArray[i + 1];
				floatArray2[i] = (sum2 * floatArray[i + 1]) + floatArray2[i];
			}
		}
	}
}

static void lpcCorrelation(unsigned int frameSize, const float * src, unsigned int totalFrames, float * dest)
{
	unsigned int	i;

	while (totalFrames--)
	{
		const float *	ptr;
		float			val;

		val = 0.0;
		i = 0;
		ptr = &src[totalFrames];
		if (frameSize - totalFrames > 15)
		{
			while (i < frameSize - totalFrames - 15)
			{
				val += (src[i] * ptr[i]
					+ src[i+1] * ptr[i + 1]
					+ src[i+2] * ptr[i + 2]
					+ src[i+3] * ptr[i + 3]
					+ src[i+4] * ptr[i + 4]
					+ src[i+5] * ptr[i + 5]
					+ src[i+6] * ptr[i + 6]
					+ src[i+7] * ptr[i + 7]
					+ src[i+8] * ptr[i + 8]
					+ src[i+9] * ptr[i + 9]
					+ src[i+10] * ptr[i + 10]
					+ src[i+11] * ptr[i + 11]
					+ src[i+12] * ptr[i + 12]
					+ src[i+13] * ptr[i + 13]
					+ src[i+14] * ptr[i + 14]
					+ src[i+15] * ptr[i + 15]);
				i += 16;
			}
		}
		
		while (i < frameSize - totalFrames)
		{
			val += (src[i] * ptr[i]);
			++i;
		}

		dest[totalFrames] = val;
	}
}

static void lpcDecode(struct SFARKINFO * sfarkInfo, const int32_t * array, unsigned int frameSize, unsigned int numFrames, const int16_t * src, int16_t * dest)
{
	unsigned int	i;
	int32_t			sum, temp;

    while (numFrames--)
    {
		sum = *src++;

		i = frameSize;
		while (i--)
		{
			temp = sfarkInfo->u.v2.u.Lpc.DecodeHist[i] * array[i];
			if (temp < 0)
				temp = -(-temp >> 14);
			else
				temp >>= 14;
			sum -= temp;
			temp = sum * array[i];
			if (temp < 0)
				temp = -(-temp >> 14);
			else
				temp >>= 14;
			sfarkInfo->u.v2.u.Lpc.DecodeHist[i + 1] = temp + sfarkInfo->u.v2.u.Lpc.DecodeHist[i];
		}

		*dest++ = sfarkInfo->u.v2.u.Lpc.DecodeHist[0] = sum;
	}
}

static void lpcAddAcHist(const float * src, unsigned int frameSize, float * dest)
{
	unsigned int	frameEnd;
	unsigned int	i;

	frameEnd = frameSize - 1;
	while (frameSize--)
	{
		const float *	ptr;
		float			val;

		val = 0.0;
		ptr = &src[frameSize];
		i = frameEnd - frameSize;
		while (i < frameEnd)
		{
			val += (src[i] * ptr[i]);
			++i;
		}

		dest[frameSize] += val;
	}
}

static void lpcClear(struct SFARKINFO * sfarkInfo)
{
	sfarkInfo->u.v2.LpcCurrHistNum = 0;
	ZeroMemory(&sfarkInfo->u.v2.u.Lpc, sizeof(sfarkInfo->u.v2.u.Lpc));
}

static void lpcUnpack(struct SFARKINFO * sfarkInfo, unsigned int dataSizeInWords, uint32_t maskVal)
{
	int16_t *			dest;
	const int16_t *		src; 
	const int16_t *		srcEnd;
	unsigned int		cntToDo;
	unsigned int		frameSize;

	dest = (int16_t *)sfarkInfo->WorkBuffer2;
	src = (int16_t *)sfarkInfo->WorkBuffer1;
	frameSize = sfarkInfo->u.v2.NumShortsInLpcFrame;

	srcEnd = &src[dataSizeInWords];
	cntToDo = dataSizeInWords;
	do
	{
		if (cntToDo >= 128)
		{
			unsigned int	numFrames, i, j;
			unsigned int	mask;

			mask = maskVal;

			numFrames = 128;

			for (i = 0; i < dataSizeInWords; i += numFrames)
			{
				if (mask & 1)
				{
					lpcClear(sfarkInfo);
					for (j = 0; j < numFrames; ++j) dest[j + i] = src[j + i];
				}
				else
				{
					int32_t		intArray[128];

					schur(sfarkInfo, frameSize, &intArray[0]);
					lpcDecode(sfarkInfo, &intArray[0], frameSize, numFrames, &src[i], &dest[i]);
				}

				mask >>= 1;

				{
				float		sumtable[256];

				j = numFrames;
				for (j = 0; j < frameSize; ++j) sumtable[j] = (float)sfarkInfo->u.v2.u.Lpc.History[j];

				j = numFrames;
				if (frameSize > j) j = frameSize;
				while (j--) sumtable[frameSize + j] = (float)dest[i + j];
				lpcAddAcHist(&sumtable[0], frameSize + 1, &sfarkInfo->u.v2.u.Lpc.AcHist[129 * sfarkInfo->u.v2.LpcCurrHistNum]);

//				for (j = 0; j < numFrames; ++j) sumtable[frameSize + j] = (float)dest[i + j];
				if (++sfarkInfo->u.v2.LpcCurrHistNum >= 4) sfarkInfo->u.v2.LpcCurrHistNum = 0;
				lpcCorrelation(numFrames, &sumtable[frameSize], frameSize + 1, &sfarkInfo->u.v2.u.Lpc.AcHist[129 * sfarkInfo->u.v2.LpcCurrHistNum]);
				}

				for (j = 0; j < frameSize; ++j) sfarkInfo->u.v2.u.Lpc.History[j] = dest[j + i];
			}
		}
		else
			CopyMemory(dest, src, cntToDo * sizeof(int16_t));

		src += dataSizeInWords;
		dest += dataSizeInWords;
		cntToDo -= dataSizeInWords;
	} while (src < srcEnd);
}

































































// ===============================================================
// Delta compression decoding, and Bit packing decoder
// ===============================================================

static void delta1(int16_t * destptr, int16_t * srcptr, unsigned int dataSizeInWords, int16_t * prevVal)
{
	int16_t *	dataend;

	dataend = &srcptr[dataSizeInWords];
	*destptr++ = *prevVal + *srcptr++;
	while (srcptr < dataend)
	{
		*destptr = *(destptr - 1) + *srcptr;
		++destptr;
		++srcptr;
	}

	*prevVal = *(destptr - 1);
}

static void delta2(int16_t * dest, int16_t * src, unsigned int dataSizeInWords, int16_t * prevVal)
{
	int16_t *	srcptr;
	int16_t *	destptr;

	srcptr = &src[dataSizeInWords - 1];
	destptr = &dest[dataSizeInWords - 1];
	*destptr-- = *srcptr--;
	while (srcptr > src)
	{
		*destptr = ((destptr[1] + srcptr[-1]) >> 1) + srcptr[0];
		--srcptr;
		--destptr;
	}
	*destptr = (destptr[1] >> 1) + srcptr[0];
	*prevVal = destptr[dataSizeInWords - 1];
}

static void delta3(int16_t * dest, int16_t * src, unsigned int dataSizeInWords, int16_t * prevVal)
{
	int16_t *	srcEnd;
	int16_t		prevVal2;
	int16_t		temp;

	prevVal2 = *prevVal;
	srcEnd = &src[dataSizeInWords];
	while (src < srcEnd)
	{
		*dest = prevVal2 + *src;
		if (*src < 0)
			temp = -(-*src >> 1);
		else
			temp = *src >> 1;
		prevVal2 += temp;
		++src;
		++dest;
	}

	*prevVal = prevVal2;
}

static unsigned int shiftBitsLookup(unsigned int val)
{
	unsigned int	low;
	unsigned char	bitN;

	if (val > 1)
	{
		low = 4;
		bitN = 2;
		while (val >= low)
		{
			++bitN;
			low <<= 1;
		}
		val = bitN;
	}

	return val;
}

static void applyShift(int16_t * dataptr, unsigned int dataSizeInWords, uint16_t * srcptr)
{
	int16_t *		endptr;

	endptr = &dataptr[dataSizeInWords];
	while (dataptr < endptr)
	{
		short	val;

		if ((val = *srcptr++))
		{
			unsigned int	i;

			i = 64;
			if (dataptr + i > endptr) i = ((char *)endptr - (char *)dataptr) / sizeof(int16_t);
			while (i--) *dataptr++ <<= val;
		}
		else
			dataptr += 64;
	}
}





static int bitRegisterFill(struct SFARKINFO * sfarkInfo)
{
	if (sfarkInfo->NumBitsInRegister < 16)
	{
		if (sfarkInfo->InputBufIndex >= sizeof(sfarkInfo->InputBuf)/sizeof(uint16_t))
		{
			if (readSfarkFile(sfarkInfo, sfarkInfo->InputBuf, sizeof(sfarkInfo->InputBuf)) <= 0) return SFARKERR_SFARKREAD;
			sfarkInfo->InputBufIndex = 0;
		}

		sfarkInfo->BitRegister = sfarkInfo->InputBuf[sfarkInfo->InputBufIndex++] | (sfarkInfo->BitRegister << 16);
		sfarkInfo->NumBitsInRegister += 16;
	}

	return 0;
}

static int bitRead(struct SFARKINFO * sfarkInfo, register unsigned char bitsToRead)
{
	register int retVal;

	if (bitRegisterFill(sfarkInfo)) return 0;

	sfarkInfo->NumBitsInRegister -= bitsToRead;
	retVal = (sfarkInfo->BitRegister >> sfarkInfo->NumBitsInRegister) & 0xFFFF;
	sfarkInfo->BitRegister &= ((1 << sfarkInfo->NumBitsInRegister) - 1);
	return retVal;
}

static unsigned char bitReadFlag(struct SFARKINFO * sfarkInfo)
{
	return bitRead(sfarkInfo, 1) ? 1 : 0;
}

static int bitReadBuf(struct SFARKINFO * sfarkInfo, unsigned char * bufptr, unsigned int bytesToRead)
{
	while (bytesToRead--)
	{
		if (bitRegisterFill(sfarkInfo)) return SFARKERR_SFARKREAD;

		sfarkInfo->NumBitsInRegister -= 8;
		*bufptr++ = (unsigned char)(sfarkInfo->BitRegister >> sfarkInfo->NumBitsInRegister);
		sfarkInfo->BitRegister &= ((1 << sfarkInfo->NumBitsInRegister) - 1);
	}

	return 0;
}






/*********************** unpackBitData() ***********************
 * Unpacks (and returns) the next of bit-packed "encode count".
 *
 * prevVal =	The previous count (will be added to the
 *				returned count to create the final count).
 *
 * RETURN: Encode count if success, or negative error number
 * other than -1 or -2).
 */

static short getBitPackCnt(struct SFARKINFO * sfarkInfo, short prevVal)
{
	short		retVal;

	retVal = 0;

	// Find the bit-pack "marker"
	while (!sfarkInfo->BitRegister)
	{
		retVal += sfarkInfo->NumBitsInRegister;
		if (sfarkInfo->InputBufIndex >= sizeof(sfarkInfo->InputBuf)/sizeof(uint16_t))
		{
			if (readSfarkFile(sfarkInfo, sfarkInfo->InputBuf, sizeof(sfarkInfo->InputBuf)) <= 0)
bad:			return 0;
			sfarkInfo->InputBufIndex = 0;
		}

		sfarkInfo->BitRegister = sfarkInfo->InputBuf[sfarkInfo->InputBufIndex++];
		sfarkInfo->NumBitsInRegister = 16;
	}

	retVal += sfarkInfo->NumBitsInRegister;
	do
	{
		--sfarkInfo->NumBitsInRegister;
	} while ((sfarkInfo->BitRegister >> sfarkInfo->NumBitsInRegister) != 1);

	sfarkInfo->BitRegister &= ((1 << sfarkInfo->NumBitsInRegister) - 1);

	retVal -= (sfarkInfo->NumBitsInRegister + 1);
	if (retVal)
	{
		if (bitRegisterFill(sfarkInfo)) goto bad;
 
 		--sfarkInfo->NumBitsInRegister;
		if ((sfarkInfo->BitRegister >> sfarkInfo->NumBitsInRegister) & 0xFFFF) retVal = -retVal;
		sfarkInfo->BitRegister &= ((1 << sfarkInfo->NumBitsInRegister) - 1);
	}

	return prevVal + retVal;
}





/********************** unpackBitData() *********************
 * Unpacks the next block of bit-packed data. The data is
 * divided up into 256 or 32 16-bit "frames", depending upon
 * whether "turbo" or "fast" method, respectively, was used
 * to create the sfark file. Then those 256 or 32 words are
 * bit-packed into a bitstream of the number of bits of the
 * highest value. In other words, a bitpacked version of the
 * frame is created. Before this is written to the sfark, a
 * bit-packed "encode count" is written, which tells how many
 * bits each word was reduced to. But note that this count is
 * added to the previous frame's count, to arrive at the final
 * value. This is done to try to reduce the size of storing the
 * count itself. Also note the encode count is initially
 * assumed 8 bits.
 *
 * RETURN: 0 if success, or negative error number.
 */

static int unpackBitData(struct SFARKINFO * sfarkInfo, unsigned int dataSizeInWords)
{
	uint16_t *	dataptr;

	dataptr = (uint16_t *)sfarkInfo->WorkBuffer1;
	while (dataSizeInWords)
	{
		unsigned int framesize;

		// We're going to decompress the data into 256 or 32 shorts
		if (sfarkInfo->CompressType == 4)	
			framesize = 256;
		else
			framesize = 32;
		if (framesize > dataSizeInWords) framesize = dataSizeInWords;
		dataSizeInWords -= framesize;

		sfarkInfo->u.v2.BitFramePackMethod = getBitPackCnt(sfarkInfo, sfarkInfo->u.v2.BitFramePackMethod);
		if (sfarkInfo->u.v2.BitFramePackMethod == SFARKERR_SFARKREAD) goto err;		// NOTE: SFARKERR_SFARKREAD must be a negative value, but not -1 or -2

		if ((unsigned short)sfarkInfo->u.v2.BitFramePackMethod > 13)
		{
			if (sfarkInfo->u.v2.BitFramePackMethod == 14)
			{
				do
				{
					if (bitRegisterFill(sfarkInfo))
err:					return SFARKERR_SFARKREAD;

					sfarkInfo->NumBitsInRegister -= 16;
					*dataptr++ = sfarkInfo->BitRegister >> sfarkInfo->NumBitsInRegister;
					sfarkInfo->BitRegister &= ((1 << sfarkInfo->NumBitsInRegister) - 1);
				} while (--framesize);
			}

			else if (sfarkInfo->u.v2.BitFramePackMethod == -1)
			{
				do
				{
					if (bitRegisterFill(sfarkInfo)) goto err;

					--sfarkInfo->NumBitsInRegister;
					*dataptr++ = -(sfarkInfo->BitRegister >> sfarkInfo->NumBitsInRegister);
					sfarkInfo->BitRegister &= ((1 << sfarkInfo->NumBitsInRegister) - 1);
				} while (--framesize);
			}

			else if (sfarkInfo->u.v2.BitFramePackMethod == -2)
			{
				ZeroMemory(dataptr, framesize * sizeof(uint16_t));
				dataptr += framesize;
			}

			else
				return SFARKERR_BADUNPACK;
		}

		else do
		{
			short			result3;
			uint32_t		oldBits;

			if (bitRegisterFill(sfarkInfo)) goto err;

			result3 = 0;
			sfarkInfo->NumBitsInRegister -= (sfarkInfo->u.v2.BitFramePackMethod + 1);
			oldBits = sfarkInfo->BitRegister >> sfarkInfo->NumBitsInRegister;
			sfarkInfo->BitRegister &= ((1 << sfarkInfo->NumBitsInRegister) - 1);
			while (!sfarkInfo->BitRegister)
			{
				result3 += sfarkInfo->NumBitsInRegister;
				if (sfarkInfo->InputBufIndex >= sizeof(sfarkInfo->InputBuf)/sizeof(uint16_t))
				{
					if (readSfarkFile(sfarkInfo, sfarkInfo->InputBuf, sizeof(sfarkInfo->InputBuf)) <= 0) goto err;
					sfarkInfo->InputBufIndex = 0;
				}

				sfarkInfo->NumBitsInRegister = 16;
				sfarkInfo->BitRegister = sfarkInfo->InputBuf[sfarkInfo->InputBufIndex++];
			}

			{
			short	oldRem;

			oldRem = sfarkInfo->NumBitsInRegister + result3;
			do
			{
				--sfarkInfo->NumBitsInRegister;
			} while ((sfarkInfo->BitRegister >> sfarkInfo->NumBitsInRegister) != 1);

			sfarkInfo->BitRegister &= ((1 << sfarkInfo->NumBitsInRegister) - 1);
			*dataptr++ = -(oldBits & 1) ^ (((signed int)oldBits >> 1) | ((uint16_t)(oldRem - (sfarkInfo->NumBitsInRegister + 1)) << sfarkInfo->u.v2.BitFramePackMethod));
			}
		} while (--framesize);
	}

	return 0;
}





/********************* addToChksum() *********************
 * sfark V2 checksum.
 */

static void addToChksum(struct SFARKINFO * sfarkInfo, unsigned int dataSizeInWords)
{
	register int32_t	sum;
	register int16_t *	dataEnd;
	register int16_t *	dataptr;

	sum = 0;
	dataptr = (int16_t *)sfarkInfo->WorkBuffer1;
	dataEnd = &dataptr[dataSizeInWords];
	while (dataptr < dataEnd)
	{
		sum += (((int32_t)*dataptr) >> 15) ^ ((int32_t)*dataptr);
		++dataptr;
	}

	sfarkInfo->RunningChksum = sum + (2 * sfarkInfo->RunningChksum);
}





/********************** uncompressTurbo() *********************
 * Unpacks the next block of bit-packed data for an sfark file
 * compressed with the "turbo" method.
 *
 * RETURN: 0 if success, or negative error number.
 */

static int uncompressTurbo(struct SFARKINFO * sfarkInfo, unsigned int dataSizeInWords)
{
	int			encodeCnt;
	int			result;

	encodeCnt = getBitPackCnt(sfarkInfo, sfarkInfo->u.v2.PrevUnpack2EncodeCnt);
	if (encodeCnt < 0 || encodeCnt > sfarkInfo->u.v2.Unpack2EncodeLimit)
	{
		result = SFARKERR_BADCNT;
		goto bad;
	}
	sfarkInfo->u.v2.PrevUnpack2EncodeCnt = encodeCnt;

	result = unpackBitData(sfarkInfo, dataSizeInWords);
	if (result < 0)
bad:	return result;

	if (encodeCnt)
	{
		int16_t *	ptr;
		void *		tempptr;

		ptr = &sfarkInfo->u.v2.PrevDataShiftVal[--encodeCnt];
		do
		{
			if (!encodeCnt)	addToChksum(sfarkInfo, dataSizeInWords);

			delta1((int16_t *)sfarkInfo->WorkBuffer2, (int16_t *)sfarkInfo->WorkBuffer1, dataSizeInWords, ptr--);

			tempptr = sfarkInfo->WorkBuffer1;
			sfarkInfo->WorkBuffer1 = sfarkInfo->WorkBuffer2;
		 	sfarkInfo->WorkBuffer2 = (unsigned char *)tempptr;

		} while (--encodeCnt >= 0);
 	}

	return 0;
}





/********************** uncompressFast() *********************
 * Unpacks the next block of bit-packed data for an sfark file
 * compressed with the "fast" method.
 *
 * RETURN: 0 if success, or negative error number.
 */

static int uncompressFast(struct SFARKINFO * sfarkInfo, unsigned int dataSizeInWords)
{
	int				result;
	unsigned short	shiftArray[76];
	unsigned char	shiftFlag;
	unsigned char	delta3flag;

	// If the shift flag is set, setup shiftArray[]
	if ((shiftFlag = bitReadFlag(sfarkInfo)))
	{
		unsigned int	i, shiftPos, calcShiftPos;

		shiftPos = i = 0;
		calcShiftPos = (dataSizeInWords + 63) / 64;
		if (calcShiftPos > 76) goto shterr;

		while (bitReadFlag(sfarkInfo))
		{
			short			encodeCnt;

			shiftPos += bitRead(sfarkInfo, shiftBitsLookup(calcShiftPos - shiftPos - 1));

			if (sfarkInfo->u.v2.CalcShiftEncodeCnt)
				encodeCnt = getBitPackCnt(sfarkInfo, 0);
			else
				sfarkInfo->u.v2.ShiftEncodeCnt = encodeCnt = getBitPackCnt(sfarkInfo, sfarkInfo->u.v2.ShiftEncodeCnt);

			if (shiftPos > calcShiftPos)
			{
shterr:			result = SFARKERR_BADSHIFTCNT;
				goto fastOut;
			}

			while (i < shiftPos) shiftArray[i++] = sfarkInfo->u.v2.CalcShiftEncodeCnt;

			sfarkInfo->u.v2.CalcShiftEncodeCnt = encodeCnt;
		}

		while (i < calcShiftPos) shiftArray[i++] = sfarkInfo->u.v2.CalcShiftEncodeCnt;
	}


	{
	unsigned char	flagArray[24];
	int				encodeCnt;

	// The next bit tells whether the delta decomp uses method 3 (versus 2 or 1)
	if ((delta3flag = bitReadFlag(sfarkInfo)))
	{
		encodeCnt = getBitPackCnt(sfarkInfo, sfarkInfo->u.v2.PrevUnpack3EncodeCnt);
		if (encodeCnt < 0 || encodeCnt > sfarkInfo->u.v2.Unpack3EncodeLimit)
		{
badcnt:		result = SFARKERR_BADCNT;
fastOut:	return result;
		}
		sfarkInfo->u.v2.PrevUnpack3EncodeCnt = encodeCnt;
	}
	else
	{
		encodeCnt = getBitPackCnt(sfarkInfo, sfarkInfo->u.v2.PrevUnpack2EncodeCnt);
		if (encodeCnt < 0 || encodeCnt > sfarkInfo->u.v2.Unpack2EncodeLimit) goto badcnt;

		sfarkInfo->u.v2.PrevUnpack2EncodeCnt = encodeCnt;
		for (result = 0; result < encodeCnt; ++result) flagArray[result] = bitReadFlag(sfarkInfo);
	}

	// If LPC compression is also used, we first read a 32-bit mask, later passed to lpcUnpack
	{
	register uint32_t	lpcMask;

	if (sfarkInfo->CompressType != 5)
	{
		lpcMask = 0;
		if (bitReadFlag(sfarkInfo))
		{
			uint32_t	temp4;

			temp4 = bitRead(sfarkInfo, 16);
			lpcMask = (bitRead(sfarkInfo, 16) << 16) | temp4;
		}
	}

	if ((result = unpackBitData(sfarkInfo, dataSizeInWords)) < 0) goto fastOut;

	if (sfarkInfo->CompressType != 5)
	{
		lpcUnpack(sfarkInfo, dataSizeInWords, lpcMask);
		goto swap;
	}
	}

	while (encodeCnt--)
	{
		register void *	tempptr;

		tempptr = &sfarkInfo->u.v2.PrevDataShiftVal[encodeCnt];
 		if (delta3flag)
			delta3((int16_t *)sfarkInfo->WorkBuffer2, (int16_t *)sfarkInfo->WorkBuffer1, dataSizeInWords, tempptr);
		else if (flagArray[encodeCnt])
		{
			if (flagArray[encodeCnt] == 1)
				delta2((int16_t *)sfarkInfo->WorkBuffer2, (int16_t *)sfarkInfo->WorkBuffer1, dataSizeInWords, tempptr);
		}
		else
			delta1((int16_t *)sfarkInfo->WorkBuffer2, (int16_t *)sfarkInfo->WorkBuffer1, dataSizeInWords, tempptr);

swap:	tempptr = sfarkInfo->WorkBuffer1;
		sfarkInfo->WorkBuffer1 = sfarkInfo->WorkBuffer2;
		sfarkInfo->WorkBuffer2 = (unsigned char *)tempptr;
	}

	if (shiftFlag) applyShift((int16_t *)sfarkInfo->WorkBuffer1, dataSizeInWords, &shiftArray[0]);

	addToChksum(sfarkInfo, dataSizeInWords);
	}

 	return 0;
}



























static uint32_t get_ulong(unsigned char * ptr)
{
	return ((uint32_t)ptr[0]) | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}





static uint16_t get_ushort(unsigned char * ptr)
{
	return ((uint16_t)ptr[0]) | ((uint16_t)ptr[1] << 8);
}



























// ==========================================================
// Extract version 1 Sfark
// ==========================================================


static const uint32_t ChecksumVals[256] = {0,
	0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
	0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E,
	0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
	0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D,
	0x6DDDE4EB, 0xF4D4B551, 0x83D385C7, 0x136C9856, 0x646BA8C0,
	0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63,
	0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
	0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA,
	0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75,
	0xDCD60DCF, 0xABD13D59, 0x26D930AC, 0x51DE003A, 0xC8D75180,
	0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
	0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87,
	0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
	0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5,
	0xE8B8D433, 0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
	0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01, 0x6B6B51F4,
	0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B,
	0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA,
	0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
	0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541,
	0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC,
	0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F,
	0xDD0D7CC9, 0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
	0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F, 0x5EDEF90E,
	0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
	0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C,
	0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
	0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B,
	0x9309FF9D, 0x0A00AE27, 0x7D079EB1, 0xF00F9344, 0x8708A3D2,
	0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671,
	0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
	0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8,
	0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767,
	0x3FB506DD, 0x48B2364B, 0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6,
	0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
	0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795,
	0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
	0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B,
	0x5BDEAE1D, 0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
	0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713, 0x95BF4A82,
	0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D,
	0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8,
	0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
	0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF,
	0xF862AE69, 0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE,
	0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D,
	0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
	0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9, 0xBDBDF21C,
	0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
	0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02,
	0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D};

static const uint32_t	BitsMasks[17] = {0x0000, 0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F, 0x00FF, 0x01FF, 0x03FF, 0x07FF, 0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF, 0x0FFFF};

static const uint16_t	DataWordMap1[31] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0};
static const uint16_t	DataWordMap2[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

static const unsigned char	EncodeCntMap1[31] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 99, 99};
static const unsigned char	EncodeCntMap2[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static const unsigned char	DataOffsets[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

 




static int readSfarkByte(struct SFARKINFO * sfarkInfo)
{
	register unsigned char *	ptr;

	ptr = (unsigned char *)sfarkInfo->InputBuf;

	if (sfarkInfo->InputBufIndex >= sizeof(sfarkInfo->InputBuf))
	{
		if (readSfarkFile(sfarkInfo, ptr, sizeof(sfarkInfo->InputBuf)) <= 0)
//			return SFARKERR_SFARKREAD;
			return 0;
		sfarkInfo->InputBufIndex = 0;
 	}

	return ptr[sfarkInfo->InputBufIndex++];
}





static unsigned int readSfarkBits(struct SFARKINFO * sfarkInfo, unsigned short numOfBits)
{
	unsigned int	i, result;

	for (i = sfarkInfo->NumBitsInRegister; i < numOfBits; i += 8) sfarkInfo->BitRegister |= readSfarkByte(sfarkInfo) << i;
	result = sfarkInfo->BitRegister & BitsMasks[numOfBits];
	sfarkInfo->NumBitsInRegister = i - numOfBits;
	sfarkInfo->BitRegister >>= numOfBits;
	return result;
}





static void freeMemList(struct PACKITEM **list)
{
	struct PACKITEM *	mem;
	struct PACKITEM *	next;

	next = *list;
	while (next)
	{
		mem = next - 1;
		next = mem->u.NextItemArray;
		GlobalFree(mem);
	}

	*list = 0;
}





static int makePackItemList(unsigned int * dataArray, unsigned int dataArraySize, unsigned int lowLimit, const uint16_t * dataWordMap, const unsigned char *encodeCntMap, struct PACKITEM ** packArrayList, unsigned int * encodeCntPtr)
{
	int				result;
	unsigned int	i;
	int				array1[17];

	ZeroMemory(&array1[0], sizeof(array1));
	i = dataArraySize;
	while (i--) ++array1[dataArray[i]];

	if (array1[0] == dataArraySize)
	{
		*packArrayList = 0;
		*encodeCntPtr = 0;
		result = 0;
	}
	else
	{
		int					val;
		unsigned int		encodeCnt, highOffset, lowOffset;

		encodeCnt = *encodeCntPtr;

		lowOffset = 1;
		while (lowOffset < 17 && !array1[lowOffset]) ++lowOffset;

		highOffset = 16;
		while (highOffset && !array1[highOffset]) --highOffset;

		if (lowOffset > encodeCnt) encodeCnt = lowOffset;
		if (encodeCnt > highOffset) encodeCnt = highOffset;
		*encodeCntPtr = encodeCnt;

		val = 1 << lowOffset;
		if (lowOffset >= highOffset)
		{
			struct PACKITEM *	memArray[16];
			unsigned int *		bigArrayPtr;
			unsigned int		numItems;
			int					curMemArrayIndex;
			unsigned int		cnt;
			int					encodeCntDiff;
			unsigned int		array3[17];
			unsigned int		bigArray[288];

start:		if (val < array1[highOffset]) goto ret2;
			val -= array1[highOffset];
			array1[highOffset] += val;

			{
			unsigned int *	ptr;
			unsigned int *	ptr3;

			cnt = 0;
			ptr = (unsigned int *)&array1[1];
			array3[1] = cnt = 0;
			ptr3 = &array3[2];
			i = highOffset;
			while (1)
			{
 				char temp;
 
				temp = i-- == 1;
				if (temp) break;
				cnt += *ptr++;
				*ptr3++ = cnt;
			}
			}

			i = 0;
			do
			{
				unsigned int temp;

				if (dataArray[i])
				{
					temp = array3[dataArray[i]]++;
					bigArray[temp] = i;
				}
			} while (++i < dataArraySize);
			array3[0] = 0;

			i = 0;
			bigArrayPtr = &bigArray[0];
			curMemArrayIndex = -1;
			encodeCntDiff = -encodeCnt;
			if (lowOffset > highOffset)
out:			result = val && highOffset != 1;
			else
			{
				for (;;)
				{
					cnt = array1[lowOffset];
findNewLow:			if (cnt--) break;
					if (++lowOffset > highOffset) goto out;
				}

				// ----------------------
				while (1)
				{
					if ((signed int)(encodeCnt + encodeCntDiff) >= (signed int)lowOffset)
					{
						struct PACKITEM		item;

						if (curMemArrayIndex == -1) goto bad;

						item.BitShiftAmt = lowOffset - encodeCntDiff;
						item.EncodeCnt = 99;

						if (&bigArray[dataArraySize] > bigArrayPtr)
						{
							unsigned int v31;

							v31 = *bigArrayPtr++;
							if (v31 >= lowLimit)
							{
								item.EncodeCnt = encodeCntMap[v31 - lowLimit];
								item.u.Data1 = dataWordMap[v31 - lowLimit];
							}
							else
							{
								item.EncodeCnt = 16;
								if (v31 >= 256) item.EncodeCnt = 15;
								item.u.Data1 = (unsigned short)v31;
							}
						}

						{
						unsigned int between, j;

						between = 1 << (lowOffset - encodeCntDiff);
						for (j = i >> encodeCntDiff; numItems > j; j += between)
							CopyMemory((char *)memArray[curMemArrayIndex] + (j * sizeof(struct PACKITEM)), &item, sizeof(struct PACKITEM));

						for (j = 1 << (lowOffset - 1); j & i; j >>= 1) i ^= j;
						i ^= j;
						}

						while ((i & ((1 << encodeCntDiff) - 1)) != array3[curMemArrayIndex])
						{
							--curMemArrayIndex;
							encodeCntDiff -= encodeCnt;
						}

						goto findNewLow;
					}

					{
					struct PACKITEM *	mem;
					unsigned int		diff;
					{
					unsigned int		highBound;
					unsigned int		increment;

					encodeCntDiff += encodeCnt;
					highBound = highOffset - encodeCntDiff <= encodeCnt ? highOffset - encodeCntDiff : encodeCnt;
					diff = lowOffset - encodeCntDiff;
					increment = 1 << (lowOffset - encodeCntDiff);
					if (increment > cnt + 1)
					{
						unsigned int *	ptr;

						increment -= (cnt + 1);
						ptr = (unsigned int *)&array1[lowOffset];
						for (;;)
						{
							unsigned int	wordInc;

							if (++diff >= highBound) break;
							wordInc = 2 * increment;
							++ptr;
							if (wordInc <= *ptr) break;
							increment = wordInc - *ptr;
						}
					}
					}

					numItems = 1 << diff;

					if (++curMemArrayIndex >= 16) goto bad;

					// Alloc a MEMHEAD, followed immediately by an array of PACKARRAYITEMs
					mem = (struct PACKITEM *)GlobalAlloc(GMEM_FIXED, (sizeof(struct PACKITEM) * numItems) + sizeof(struct PACKITEM));
					if (!mem) break;

					*packArrayList = memArray[curMemArrayIndex] = &mem[1];
					packArrayList = &mem->u.NextItemArray;
					mem->u.NextItemArray = 0;

					if (curMemArrayIndex)
					{
						struct PACKITEM *	itemPtr;
						unsigned int		temp;

						array3[curMemArrayIndex] = i;

						temp = i >> (encodeCntDiff - (unsigned char)encodeCnt);
						itemPtr = memArray[curMemArrayIndex - 1] + temp;
						itemPtr->EncodeCnt = diff + 16;
						itemPtr->BitShiftAmt = encodeCnt;
						itemPtr->u.NextItemArray = memArray[curMemArrayIndex];
					}
					}
				}

				if (curMemArrayIndex) freeMemList(memArray);
bad:			result = -1;
			}
		}
		else
		{
			int *			ptr;
			unsigned int	low;

			low = lowOffset;
			ptr = &array1[lowOffset];
			while (val >= *ptr)
			{
				val -= *ptr++;
				val *= 2;
				if (++low >= highOffset) goto start;
			}

ret2:		result = 2;
		}
	}

	return result;
}





static void flushToOutBuf(struct SFARKINFO * sfarkInfo)
{
	register unsigned int	amt;

	amt = sfarkInfo->u.v1.BytesInDecompBuf - sfarkInfo->u.v1.PrevBytesInDecompBuf;
	if (amt)
	{
		CopyMemory(sfarkInfo->u.v1.OutbufPtr, &sfarkInfo->u.v1.DecompBuf[sfarkInfo->u.v1.PrevBytesInDecompBuf], amt);
		sfarkInfo->u.v1.OutbufPtr += amt;
		sfarkInfo->u.v1.PrevBytesInDecompBuf += amt;
	}
}





static unsigned char decodeData(struct SFARKINFO * sfarkInfo, unsigned int * dataRet, struct PACKITEM * arrayPtr, int encodeCnt)
{
	struct PACKITEM *	itemPtr;
	unsigned char		cnt;

	for (;;)
	{
		while (sfarkInfo->NumBitsInRegister < encodeCnt)
		{
			sfarkInfo->BitRegister |= readSfarkByte(sfarkInfo) << sfarkInfo->NumBitsInRegister;
			sfarkInfo->NumBitsInRegister += 8;
		}

		itemPtr = &arrayPtr[sfarkInfo->BitRegister & BitsMasks[encodeCnt]];
		cnt = itemPtr->EncodeCnt;
		if (cnt == 99) break;
		sfarkInfo->BitRegister >>= itemPtr->BitShiftAmt;
		sfarkInfo->NumBitsInRegister -= itemPtr->BitShiftAmt;
		if (cnt <= 16)
		{
			*dataRet = itemPtr->u.Data1;
			break;
		}

		arrayPtr = itemPtr->u.NextItemArray;
		encodeCnt = cnt - 16;
	}

	return cnt;
}





static unsigned int flushPackItemsToOutBuf(struct SFARKINFO * sfarkInfo, unsigned int len)
{
	unsigned int	i;
	unsigned int	moveAmt;
	unsigned char	numOfBits;

	i = 0;
	while (i < len)
	{
		if (sfarkInfo->u.v1.BlockFlags & 0x10) goto skip;

		numOfBits = decodeData(sfarkInfo, &sfarkInfo->u.v1.VarDecodeDWord, sfarkInfo->u.v1.PackItemArray, sfarkInfo->u.v1.FirstEncodeCnt);
		if (numOfBits == 99)
bad:		return (unsigned int)-1;

		if (numOfBits != 16)
		{
			if (numOfBits == 15)
			{
				sfarkInfo->u.v1.BlockFlags &= 0xC0;
				break;
			}

			sfarkInfo->u.v1.VarDecodeDWord += readSfarkBits(sfarkInfo, numOfBits);
			numOfBits = decodeData(sfarkInfo, &sfarkInfo->u.v1.VarDecodeByteCnt, sfarkInfo->u.v1.CurPackItemArray, sfarkInfo->u.v1.SecondEncodeCnt);
			if (numOfBits == 99) goto bad;

			sfarkInfo->u.v1.VarDecodeByteCnt = sfarkInfo->u.v1.BytesInDecompBuf - (sfarkInfo->u.v1.VarDecodeByteCnt + readSfarkBits(sfarkInfo, numOfBits));

skip:		sfarkInfo->u.v1.BlockFlags &= ~0x10;
			do
			{
				sfarkInfo->u.v1.VarDecodeByteCnt &= 0x7FFF;
				if (sfarkInfo->u.v1.VarDecodeByteCnt <= sfarkInfo->u.v1.BytesInDecompBuf)
					moveAmt = sfarkInfo->u.v1.BytesInDecompBuf;
				else
					moveAmt = sfarkInfo->u.v1.VarDecodeByteCnt;
				moveAmt = 32768 - moveAmt;
				if (moveAmt > sfarkInfo->u.v1.VarDecodeDWord) moveAmt = sfarkInfo->u.v1.VarDecodeDWord;
				if (moveAmt + i > len)
				{
					sfarkInfo->u.v1.BlockFlags |= 0x10;
					moveAmt = len - i;
				}
				i += moveAmt;
				sfarkInfo->u.v1.VarDecodeDWord -= moveAmt;

				if (moveAmt > sfarkInfo->u.v1.BytesInDecompBuf - sfarkInfo->u.v1.VarDecodeByteCnt)
				{
					do
					{
						sfarkInfo->u.v1.DecompBuf[sfarkInfo->u.v1.BytesInDecompBuf++] = sfarkInfo->u.v1.DecompBuf[sfarkInfo->u.v1.VarDecodeByteCnt++];
					} while (--moveAmt);
				}
				else
				{
					CopyMemory(&sfarkInfo->u.v1.DecompBuf[sfarkInfo->u.v1.BytesInDecompBuf], &sfarkInfo->u.v1.DecompBuf[sfarkInfo->u.v1.VarDecodeByteCnt], moveAmt);
					sfarkInfo->u.v1.BytesInDecompBuf += moveAmt;
					sfarkInfo->u.v1.VarDecodeByteCnt += moveAmt;
				}

				if (sfarkInfo->u.v1.BytesInDecompBuf >= 32768)
				{
					flushToOutBuf(sfarkInfo);
					sfarkInfo->u.v1.BytesInDecompBuf = sfarkInfo->u.v1.PrevBytesInDecompBuf = 0;
				}

			} while (sfarkInfo->u.v1.VarDecodeDWord && !(sfarkInfo->u.v1.BlockFlags & 0x10));
		}
		else
		{
			sfarkInfo->u.v1.DecompBuf[sfarkInfo->u.v1.BytesInDecompBuf++] = (unsigned char)sfarkInfo->u.v1.VarDecodeDWord;
			if (sfarkInfo->u.v1.BytesInDecompBuf >= 32768)
			{
				flushToOutBuf(sfarkInfo);
				sfarkInfo->u.v1.BytesInDecompBuf = sfarkInfo->u.v1.PrevBytesInDecompBuf = 0;
			}
			++i;
		}
	}

	flushToOutBuf(sfarkInfo);
	return i;
}





/*********************** decompBlock3() ***********************
 * Performs LsPack decompression on the next block of data
 * compressed via method 3, and writes it to the output buffer.
 *
 * RETURN: # of bytes decompressed if success, or negative
 * error number.
 */

static unsigned int decompBlock3(struct SFARKINFO * sfarkInfo, unsigned int blockSize)
{
	unsigned int	i;

	if (!(sfarkInfo->u.v1.BlockFlags & 0x20))
	{
		unsigned int *	dataArrayPtr;
		unsigned int	dataArray[316];
		unsigned int	dataArray1stCnt;
		unsigned int	dataArray2ndCnt;
		unsigned int	limit;
		unsigned int	mask;
		unsigned int	data;

		// Break off the next 5 bits as the count of additional fields we need in dataArray[] (beyond the first 257 ints)
		dataArray2ndCnt = readSfarkBits(sfarkInfo, 5) + 257;

		// Repeat the above to get the count for the first (257) fields of dataArray[]
		dataArray1stCnt = readSfarkBits(sfarkInfo, 5) + 1;

		// Error-check counts
		if (dataArray2ndCnt > 286 || dataArray1stCnt > 30) goto bad;

		// Break off the next four bits as a count of how many 3-bit vals to read into dataArray[]
		data = readSfarkBits(sfarkInfo, 4) + 4;
		i = 0;
		while (i < data) dataArray[DataOffsets[i++]] = readSfarkBits(sfarkInfo, 3);
		while (i < 19) dataArray[DataOffsets[i++]] = 0;

		sfarkInfo->u.v1.FirstEncodeCnt = 7;
		i = makePackItemList(&dataArray[0], 19, 19, 0, 0, &sfarkInfo->u.v1.PackItemArray, &sfarkInfo->u.v1.FirstEncodeCnt);
		if (i)
		{
			if (i == 1) freeMemList(&sfarkInfo->u.v1.PackItemArray);
			goto bad;
		}

		limit = dataArray1stCnt + dataArray2ndCnt;
		mask = BitsMasks[sfarkInfo->u.v1.FirstEncodeCnt];
		data = i = 0;

		while (i < limit)
		{
			while (sfarkInfo->NumBitsInRegister < sfarkInfo->u.v1.FirstEncodeCnt)
			{
				sfarkInfo->BitRegister |= readSfarkByte(sfarkInfo) << sfarkInfo->NumBitsInRegister;
				sfarkInfo->NumBitsInRegister += 8;
			}

			{
			unsigned char	shiftAmt;

			sfarkInfo->u.v1.CurPackItemArray = &sfarkInfo->u.v1.PackItemArray[sfarkInfo->BitRegister & mask];
			shiftAmt = sfarkInfo->u.v1.CurPackItemArray->BitShiftAmt;
			sfarkInfo->BitRegister >>= shiftAmt;
			sfarkInfo->NumBitsInRegister -= shiftAmt;
			}

			{
			unsigned int nextData;

			nextData = sfarkInfo->u.v1.CurPackItemArray->u.Data1;
			if (nextData >= 16)
			{
				unsigned int cnt;

				if (nextData == 16)
					cnt = readSfarkBits(sfarkInfo, 2) + 3;
				else
				{
					data = 0;
					if (nextData == 17)
						cnt = readSfarkBits(sfarkInfo, 3) + 3;
					else
						cnt = readSfarkBits(sfarkInfo, 7) + 11;
				}

				if (cnt + i > limit) goto bad;

				dataArrayPtr = &dataArray[i];
				while (cnt--)
				{
					*(dataArrayPtr)++ = data;
					++i;
				}
			}
			else
				dataArray[i++] = data = nextData;
			}
		}

		freeMemList(&sfarkInfo->u.v1.PackItemArray);

		// =========================
		sfarkInfo->u.v1.FirstEncodeCnt = 9;
		i = makePackItemList(&dataArray[0], dataArray2ndCnt, 257, &DataWordMap1[0], &EncodeCntMap1[0], &sfarkInfo->u.v1.PackItemArray, &sfarkInfo->u.v1.FirstEncodeCnt);
		if (i)
		{
			if (i == 1) freeMemList(&sfarkInfo->u.v1.PackItemArray);
bad:		return (unsigned int)-1;
		}

		sfarkInfo->u.v1.SecondEncodeCnt = 6;
		i = makePackItemList(&dataArray[dataArray2ndCnt], dataArray1stCnt, 0, &DataWordMap2[0], &EncodeCntMap2[0], &sfarkInfo->u.v1.CurPackItemArray, &sfarkInfo->u.v1.SecondEncodeCnt);
		if (i)
		{
			if (i == 1) freeMemList(&sfarkInfo->u.v1.CurPackItemArray);
			freeMemList(&sfarkInfo->u.v1.PackItemArray);
			goto bad;
		}

		sfarkInfo->u.v1.BlockFlags |= 0x20u;
	}

	i = flushPackItemsToOutBuf(sfarkInfo, blockSize);
	if (i == (unsigned int)-1 || !(sfarkInfo->u.v1.BlockFlags & 0x20))
	{
		freeMemList(&sfarkInfo->u.v1.PackItemArray);
		freeMemList(&sfarkInfo->u.v1.CurPackItemArray);
	}

	return i;
}





/*********************** decompBlock2() ***********************
 * Performs LsPack decompression on the next block of data
 * compressed via method 2, and writes it to the output buffer.
 *
 * RETURN: # of bytes decompressed if success, or negative
 * error number.
 */

static unsigned int decompBlock2(struct SFARKINFO * sfarkInfo, unsigned int blockSize)
{
	unsigned int	i;
	int				ret;
	unsigned int	dataArray[288];

	if (!(sfarkInfo->u.v1.BlockFlags & 0x20))
	{
		i = 0;
		do
		{
			dataArray[i++] = 8;
		} while (i < 144);
		do
		{
			dataArray[i++] = 9;
		} while (i < 256);
		do
		{
			dataArray[i++] = 7;
		} while (i < 280);
		do
		{
			dataArray[i++] = 8;
		} while (i < 288);

		sfarkInfo->u.v1.FirstEncodeCnt = 7;
		ret = makePackItemList(dataArray, 288, 257, &DataWordMap1[0], &EncodeCntMap1[0], &sfarkInfo->u.v1.PackItemArray, &sfarkInfo->u.v1.FirstEncodeCnt);
		if (ret)
		{
			if (ret == 1) freeMemList(&sfarkInfo->u.v1.PackItemArray);
bad:		return (unsigned int)-1;
		}

		i = 0;
		do
		{
			dataArray[i] = 5;
		} while (++i < 30);

		sfarkInfo->u.v1.SecondEncodeCnt = 5;
		ret = makePackItemList(dataArray, 30, 0, &DataWordMap2[0], &EncodeCntMap2[0], &sfarkInfo->u.v1.CurPackItemArray, &sfarkInfo->u.v1.SecondEncodeCnt);
		if (ret & 0xFFFFFFFE)
		{
			freeMemList(&sfarkInfo->u.v1.PackItemArray);
			goto bad;
		}

		sfarkInfo->u.v1.BlockFlags |= 0x20;
	}

	ret = flushPackItemsToOutBuf(sfarkInfo, blockSize);
	if (ret == (unsigned int)-1 || !(sfarkInfo->u.v1.BlockFlags & 0x20))
	{
		freeMemList(&sfarkInfo->u.v1.PackItemArray);
		freeMemList(&sfarkInfo->u.v1.CurPackItemArray);
	}

	return ret;
}





/*********************** decompBlock1() ***********************
 * Performs LsPack decompression on the next block of data
 * compressed via method 1, and writes it to the output buffer.
 *
 * RETURN: # of bytes decompressed if success, or negative
 * error number.
 */

static unsigned int decompBlock1(struct SFARKINFO * sfarkInfo, unsigned int len)
{
	unsigned int	i;

	if (!(sfarkInfo->u.v1.BlockFlags & 0x20))
	{
		if (sfarkInfo->NumBitsInRegister > 7)
bad:		return (unsigned int)-1;

		sfarkInfo->BitRegister = sfarkInfo->NumBitsInRegister = 0;
		sfarkInfo->u.v1.EncodeCnt = readSfarkByte(sfarkInfo);
		sfarkInfo->u.v1.EncodeCnt |= readSfarkByte(sfarkInfo) << 8;
		i = readSfarkByte(sfarkInfo);
		if ((((readSfarkByte(sfarkInfo) << 8) | i) ^ 0xFFFF) != sfarkInfo->u.v1.EncodeCnt) goto bad;
		sfarkInfo->u.v1.BlockFlags |= 0x20u;
	}

	i = 0;
	if (len)
	{
		while (sfarkInfo->u.v1.EncodeCnt--)
		{
			sfarkInfo->u.v1.DecompBuf[sfarkInfo->u.v1.BytesInDecompBuf++] = readSfarkByte(sfarkInfo);
			if (sfarkInfo->u.v1.BytesInDecompBuf >= 32768)
			{
				flushToOutBuf(sfarkInfo);
				sfarkInfo->u.v1.BytesInDecompBuf = sfarkInfo->u.v1.PrevBytesInDecompBuf = 0;
			}

			if (len <= ++i) goto out;
		}
		sfarkInfo->u.v1.BlockFlags &= 0xC0u;
	}
out:
	flushToOutBuf(sfarkInfo);
	return i;
}





/********************* decompLspackBlock() *********************
 * Performs LsPack decompression on the next block of data
 * in the sfark file, and write it to the output buffer.
 * Each time this is called, it does another 1024 (or so) block
 * of data.
 *
 * RETURN: # of bytes decompressed if success, or negative
 * error number.
 */

static int decompLspackBlock(struct SFARKINFO * sfarkInfo)
{
	unsigned int	i;
	unsigned char	flags;
	unsigned int	result;

	sfarkInfo->u.v1.BlockFlags |= 0x40;
	sfarkInfo->u.v1.OutbufPtr = sfarkInfo->WorkBuffer2;
	result = i = 0;
	do
	{
		if (!(sfarkInfo->u.v1.BlockFlags & 3))
		{
			if (sfarkInfo->u.v1.BlockFlags & 0x80) break;

			if (sfarkInfo->NumBitsInRegister < 1)
			{
				sfarkInfo->BitRegister |= (readSfarkByte(sfarkInfo) << sfarkInfo->NumBitsInRegister);
				sfarkInfo->NumBitsInRegister += 8;
			}

			if (sfarkInfo->BitRegister & 1) sfarkInfo->u.v1.BlockFlags |= 0x80;
			sfarkInfo->BitRegister >>= 1;
			--sfarkInfo->NumBitsInRegister;

			if (sfarkInfo->NumBitsInRegister < 2)
			{
				sfarkInfo->BitRegister |= (readSfarkByte(sfarkInfo) << sfarkInfo->NumBitsInRegister);
				sfarkInfo->NumBitsInRegister += 8;
			}

			flags = (unsigned char)(sfarkInfo->BitRegister & 3) + 1;
			if (flags & 0xFC)
bad:			return -1;

			sfarkInfo->u.v1.BlockFlags |= flags;
			sfarkInfo->BitRegister >>= 2;
			sfarkInfo->NumBitsInRegister -= 2;
		}

		if ( (sfarkInfo->u.v1.BlockFlags & 3) == 1 )
			result = decompBlock1(sfarkInfo, 1024 - i);
		else if ((sfarkInfo->u.v1.BlockFlags & 3) == 2)
			result = decompBlock2(sfarkInfo, 1024 - i);
		else if ((sfarkInfo->u.v1.BlockFlags & 3) == 3)
			result = decompBlock3(sfarkInfo, 1024 - i);
		else goto bad;

		if (result == (unsigned int)-1) goto bad;

		if (!result && (sfarkInfo->u.v1.BlockFlags & 0x80)) break;

		i += result;
	} while (i < 1024);

	return (int)i;
}





/********************* assembleParts() *********************
 * Takes the 2 "part files", created in the temp dir, performs
 * the final delta decompression, and writes out the SF2 file.
 * each time this is called, it does another 8192 block of
 * data, and clears SFARKINFO->RunState when done.
 *
 * RETURN: 0 = success, or negative error number.
 */

static int assembleParts(struct SFARKINFO * sfarkInfo)
{
	unsigned int	i, blocksize;

	// Read another 8192 byte block from part 1 file
	if ((blocksize = readSfarkFile(sfarkInfo, sfarkInfo->WorkBuffer1, 8192+1)) > 8192+1)
bad:	return SFARKERR_SFARKREAD;
	if (!blocksize)
	{
		// Entire soundfont is now extracted
		sfarkInfo->RunState = SFARK_DONE;

		return 0;
	}
	--blocksize;

	// Read another 8192 byte block from part 2 file
	{
	HANDLE_T	fileHandle;

	fileHandle = sfarkInfo->InputFileHandle;
	sfarkInfo->InputFileHandle = sfarkInfo->u.v1.PartFileHandle;
	i = readSfarkFile(sfarkInfo, sfarkInfo->WorkBuffer1 + 8192, blocksize);
	sfarkInfo->InputFileHandle = fileHandle;
	if (i > blocksize) goto bad;
	if (i != blocksize)
		return SFARKERR_BADCOMP;
	}

	{
	uint16_t *		ptr;

	ptr = (uint16_t *)sfarkInfo->WorkBuffer2;

	// Combine input bytes (from both part files) to words, and store in out buf
	for (i = 0; i < blocksize; i++)
	{
		uint16_t		val;

		val = sfarkInfo->WorkBuffer1[i + 1] << 8;
		if (val >= 0x8000)
			val |= (uint16_t)(0xFF - sfarkInfo->WorkBuffer1[i+8192]);
		else
			val |= (uint16_t)sfarkInfo->WorkBuffer1[i+8192];

		ptr[i] = val;
	}

	// Do delta shift on out buf
	for (i = 0; i < sfarkInfo->WorkBuffer1[0]; i++)
	{
		unsigned int	j;
		uint16_t		val;
		
		val = 0;
		for (j = 0; j < blocksize; j++)
		{
			ptr[j] += val;
			val = ptr[j];
		}
	}

	// Store 16-bit words in little endian order
	for (i = 0; i < blocksize; i++)
	{
		unsigned char *	ptr2;
		uint16_t		val;

		val = ptr[i];
		ptr2 = (unsigned char *)&ptr[i];
		*ptr2++ = (unsigned char)val;
		*ptr2 = (unsigned char)(val >> 8);
	}

	// write block to soundfont
	i = writeToFontFile(sfarkInfo, ptr, blocksize << 1);
	}

	return (int)i;
}





/******************* getTempPartName() *******************
 * Formats the filename for one of the "part files", to be
 * created in the temp dir.
 *
 * partNum = '1' or '2'
 */

static void getTempPartName(struct SFARKINFO * sfarkInfo, char partNum)
{
	register unsigned int	len;
	register char *			buffer;

	buffer = (char *)sfarkInfo->WorkBuffer2;
#ifdef WIN32
	len = GetTempPathA(1024, buffer);
	if (buffer[len - 1] != '\\') buffer[len++] = '\\';
#else
	{
	register char *			tmpdir;

	if (!(tmpdir = getenv("TMPDIR"))) tmpdir = "/tmp";
	strcpy(buffer, tmpdir);
	len = strlen(buffer);
	buffer[len++] = '/';
	}
#endif
	lstrcpyA(&buffer[len], (const char *)sfarkInfo->WorkBuffer2 + 262144 - 1024);
	len += sfarkInfo->LeadingPadUncompSize;
	buffer[len++] = partNum;
	buffer[len] = 0;
}





/********************** initV1vars() ***********************
 * Reset SFARKINFO fields in prep for its extracting another
 * file.
 */

static void initV1vars(struct SFARKINFO * sfarkInfo)
{
	sfarkInfo->InputBufIndex = sizeof(sfarkInfo->InputBuf);
	sfarkInfo->RunningUncompSize = sfarkInfo->BitRegister = sfarkInfo->NumBitsInRegister = sfarkInfo->Percent = sfarkInfo->u.v1.BlockFlags = 0;
	sfarkInfo->u.v1.DecompBuf = sfarkInfo->WorkBuffer1;
	sfarkInfo->u.v1.BytesInDecompBuf = sfarkInfo->u.v1.PrevBytesInDecompBuf = 0;
	sfarkInfo->RunningChksum = -1;
}





/****************** seekNextLspackPart() *******************
 * Locates the next "part file" within the sfark file, and
 * preps for its extraction (LsPack) decompression.
 *
 * RETURN: 0 = success, or negative error number.
 */

static int seekNextLspackPart(struct SFARKINFO * sfarkInfo)
{
	unsigned char *	fileHdrStartPtr;
	int				result;
	unsigned int	headerSize, slop;

	// Flush remaining bytes in input buffer
	result = sfarkInfo->InputBufIndex;
	setSfarkFilePos(sfarkInfo, - (sizeof(sfarkInfo->InputBuf) - result));

	// Read next lspack header
	fileHdrStartPtr = sfarkInfo->WorkBuffer1;

	for (;;)
	{
		if ((headerSize = readSfarkFile(sfarkInfo, fileHdrStartPtr, 36+4)) > 36+4)
		{
bad:		result = SFARKERR_SFARKREAD;
			break;
		}
		if (headerSize != 36+4)
		{
hdr:		result = SFARKERR_BADHDR;
			break;
		}

		// Get the filename len
		headerSize = get_ushort(fileHdrStartPtr + 36);
		slop = get_ushort(fileHdrStartPtr + 38) + headerSize;

		// Read in the filename (plus an additional str we ignore)
		if ((result = readSfarkFile(sfarkInfo, fileHdrStartPtr + 100, slop)) < 0) goto bad;
		if ((unsigned int)result != slop) goto hdr;

		// The Lspack entry we want has a filename that ends in ".sfArk$1" or ".sfArk$2"
		if (headerSize > 8 && !memcmp(&fileHdrStartPtr[headerSize + 100 - 8], &SfArkId[0], sizeof(SfArkId)))
		{
			sfarkInfo->FileUncompSize = get_ulong(fileHdrStartPtr + 22);
			sfarkInfo->FileChksum = get_ulong(fileHdrStartPtr + 14);

			// Create the second part file
			getTempPartName(sfarkInfo, fileHdrStartPtr[headerSize + 100 - 1]);
			if (!(result = createSfFile(sfarkInfo, sfarkInfo->WorkBuffer2)))
			{
				sfarkInfo->Flags |= SFARK_OUT_OPEN;
				initV1vars(sfarkInfo);
			}

			break;
		}

		// Keep looking for the header
		result = get_ulong(fileHdrStartPtr + 18);
		setSfarkFilePos(sfarkInfo, result);
	}

	return result;
}





/********************* addToCrc32() *********************
 * sfark V1 checksum. The checksum is done only on each
 * (LsPack compressed) "part file" within the sfark file,
 * and not on the final soundfont file. Bad design.
 * preps for its extraction.
 */

static void addToCrc32(struct SFARKINFO * sfarkInfo, unsigned int count)
{
	register uint32_t			result;
	register unsigned char *	buffer;

	buffer = sfarkInfo->WorkBuffer2;
	result = sfarkInfo->RunningChksum;
	while (count--) result = (result >> 8) ^ ChecksumVals[(result ^ *buffer++) & 0xFF];
	sfarkInfo->RunningChksum = result;
}

















// ==============================================================
// App API
// ==============================================================


static void cleanFiles(register struct SFARKINFO *);

/*********************** SfarkExtract() **********************
 * Extracts another block of data within an sfArk file, and
 * saves it to the soundfont file.
 *
 * sf =			Handle returned by SfarkAlloc().
 *
 * RETURN: 0 = success, 1 if the soundfont is fully extracted,
 * or negative error number.
 */

int WINAPI SfarkExtract(SFARKHANDLE sf)
{
	register struct SFARKINFO *	sfarkInfo;
	register int				result;

	if ((sfarkInfo = (struct SFARKINFO *)sf) && sfarkInfo->RunState)
	{
		// Vers 1?
		if (sfarkInfo->CompressType == 2)
		{
			// Still unpacking the 2 lspack entries?
			if (sfarkInfo->RunState > 1)
			{
				result = decompLspackBlock(sfarkInfo);
				if (result >= 0)
				{
					unsigned int	uncompressDataSize;

					if (!(uncompressDataSize = (unsigned int)result)) goto corrupt;

					// Update crc32
					addToCrc32(sfarkInfo, uncompressDataSize);

					// Write the uncompressed block to the soundfont file
					if (!(result = writeToFontFile(sfarkInfo, sfarkInfo->WorkBuffer2, uncompressDataSize)))
					{
						sfarkInfo->RunningUncompSize += uncompressDataSize;
						if (sfarkInfo->RunningUncompSize >= sfarkInfo->FileUncompSize)
						{
							if (~sfarkInfo->RunningChksum != sfarkInfo->FileChksum)
								result = SFARKERR_CHKSUM;
							else
							{
								// This part is now extracted

								// Close part file we finished
								closeFile(sfarkInfo->OutputFileHandle);
								sfarkInfo->Flags &= ~SFARK_OUT_OPEN;

								// Do we need to extract the other part?
								if (--sfarkInfo->RunState > 1)
									result = seekNextLspackPart(sfarkInfo);
								else
								{
									// Create the final sf
									if ((unsigned char *)sfarkInfo->u.v1.AppFontName == sfarkInfo->WorkBuffer2)
										lstrcpyA((char *)sfarkInfo->WorkBuffer2, (const char *)sfarkInfo->WorkBuffer2 + 262144 - 1024);
									if (!(result = createSfFile(sfarkInfo, sfarkInfo->u.v1.AppFontName)))
									{
										// Close the orig sfark, and open the 2 temp part files
										closeFile(sfarkInfo->InputFileHandle);
										sfarkInfo->Flags &= ~SFARK_IN1_OPEN;
										sfarkInfo->Flags |= SFARK_OUT_OPEN;

										getTempPartName(sfarkInfo, '2');
										if (!(result = openSfarkFile(sfarkInfo, sfarkInfo->WorkBuffer2)))
										{
											sfarkInfo->Flags |= SFARK_IN2_OPEN;
											sfarkInfo->u.v1.PartFileHandle = sfarkInfo->InputFileHandle;
											getTempPartName(sfarkInfo, '1');
											if ((result = openSfarkFile(sfarkInfo, sfarkInfo->WorkBuffer2)))
												sfarkInfo->InputFileHandle = sfarkInfo->u.v1.PartFileHandle;
											else
											{
												sfarkInfo->RunningChksum = sfarkInfo->FileChksum;
												sfarkInfo->Percent = 0;
												sfarkInfo->Flags |= SFARK_IN1_OPEN;
											}
										}
									}
								}
							}
						}
					}
				}
			}

			// phase 2: Put the 2 pieces back together with additional delta decomp
			else
				result = assembleParts(sfarkInfo);
		}

		// delta
		else if (sfarkInfo->RunState == SFARK_BITPACK)
		{
			unsigned int	uncompressDataSize;
			unsigned int	numShorts;
			{
			unsigned int	bitPackEnd;
			unsigned int	runningUncompSize;

			// Check if we got to the end of the delta compressed blocks. If so, the next
			// block (after this one) will be LZIP compressed
			numShorts = sfarkInfo->u.v2.NumShortsInLpcBlock;
			uncompressDataSize = sizeof(int16_t) * numShorts;
			runningUncompSize = sfarkInfo->RunningUncompSize;
			bitPackEnd = sfarkInfo->BitPackEndOffset;
			if (uncompressDataSize + runningUncompSize >= bitPackEnd)
			{
				uncompressDataSize = bitPackEnd - runningUncompSize;
				numShorts = uncompressDataSize / sizeof(int16_t);
				sfarkInfo->RunState = SFARK_LZIP;	// Next block is LZip
			}
			}

			if (sfarkInfo->CompressType == 4)	
				result = uncompressTurbo(sfarkInfo, numShorts);
			else
				result = uncompressFast(sfarkInfo, numShorts);
			if (!result)
			{
				uint16_t *		from;
				unsigned char *	to;
				uint16_t		val;

				// Store 16-bit words in little endian order
				from = (uint16_t *)sfarkInfo->WorkBuffer1;
				while (numShorts--)
				{

					to = (unsigned char *)from;
					val = *from++;
					*to++ = (unsigned char)val;
					*to = (unsigned char)(val >> 8);
				}

				if (!(result = writeToFontFile(sfarkInfo, sfarkInfo->WorkBuffer1, uncompressDataSize)))
					sfarkInfo->RunningUncompSize += uncompressDataSize;
			}
		}

		// LZip
		else
		{
			uint32_t		blockSize;

			// Read LZip compressed blockSize (uint32_t)
			if (!(result = bitReadBuf(sfarkInfo, sfarkInfo->WorkBuffer1, sizeof(uint32_t))))
			{
				blockSize = get_ulong(sfarkInfo->WorkBuffer1);

				// An sfark compressed block has a size limit of 262144 bytes
				if (blockSize > 262144)
					result = SFARKERR_BADLEN;
				else
				{
					unsigned int		bytesExtracted;

					// Read in and unpack bit-packed bytes
					if (!(result = bitReadBuf(sfarkInfo, sfarkInfo->WorkBuffer1, blockSize)))
					{
						// Decompress LZIP'ed data into 2nd buffer and get size of uncompressed block
						sfarkInfo->u.v2.u.Zip.next_in = (Bytef *)sfarkInfo->WorkBuffer1;
						sfarkInfo->u.v2.u.Zip.avail_in = blockSize;

						sfarkInfo->u.v2.u.Zip.next_out = sfarkInfo->WorkBuffer2;
						sfarkInfo->u.v2.u.Zip.avail_out = 262144;

						sfarkInfo->u.v2.u.Zip.zalloc = (alloc_func)0;
						sfarkInfo->u.v2.u.Zip.zfree = (free_func)0;

						result = inflateInit(&sfarkInfo->u.v2.u.Zip);
						if (result == Z_OK)
						{
							result = inflate(&sfarkInfo->u.v2.u.Zip, Z_FINISH);
							if (result != Z_STREAM_END)
							{
								inflateEnd(&sfarkInfo->u.v2.u.Zip);
								if (result == Z_OK) result = Z_MEM_ERROR;
							}
							else
							{
								bytesExtracted = sfarkInfo->u.v2.u.Zip.total_out;
								result = inflateEnd(&sfarkInfo->u.v2.u.Zip);
							}
						}

						if (result)
						{
							switch (result)
							{
								case Z_MEM_ERROR:
								{
									result = SFARKERR_MEM;
									break;
								}

								// Z_DATA_ERROR if the input data was corrupted.
								default:
bad:								result = SFARKERR_BADCOMP;
							}
						}
						else
						{
							// Update checksum
							sfarkInfo->RunningChksum = adler32(sfarkInfo->RunningChksum, sfarkInfo->WorkBuffer2, bytesExtracted);

							// Write the uncompressed block to the soundfont file
							if (!(result = writeToFontFile(sfarkInfo, sfarkInfo->WorkBuffer2, bytesExtracted)))
							{
								// Inc total uncompressed size and see if the entire soundfont is now extracted
								sfarkInfo->RunningUncompSize += bytesExtracted;
								if (sfarkInfo->RunningUncompSize < sfarkInfo->FileUncompSize)
								{
									if (sfarkInfo->RunningUncompSize < sfarkInfo->LeadingPadUncompSize)
corrupt:								goto bad;

									// Only the non-waveform data of an Sfark file uses a LZIP compression. The audio data
									// is one of two delta compress schemes which we'll call "fast" and "turbo". Since we
									// just did LZIP, then the next block must be one of the delta variants
									sfarkInfo->RunState = SFARK_BITPACK;
									ZeroMemory(&sfarkInfo->u.v2.u.Zip, sizeof(z_stream));
								}
								else
									// Entire soundfont is now extracted
									sfarkInfo->RunState = SFARK_DONE;
							}
						}
					}
				}
			}
		}

		if (!result)
		{
			// Update % done
			if ((sfarkInfo->FileUncompSize - (sfarkInfo->FileUncompSize - sfarkInfo->RunningUncompSize)) > (sfarkInfo->FileUncompSize / 10) * sfarkInfo->Percent) ++sfarkInfo->Percent;

			// Entire soundfont is now extracted?
			if (!sfarkInfo->RunState)
			{
				// Test the checksum for the whole file
				result = 1;
				if (sfarkInfo->RunningChksum != sfarkInfo->FileChksum) result = SFARKERR_CHKSUM;
			}
		}

		// Close files if an error or we're done
		if (result) cleanFiles(sfarkInfo);

		return result;
	}

	return SFARKERR_APP;
}






/********************* SfarkBeginExtract() *******************
 * Begins extracting the soundfont within an sfArk file, and
 * saves it to a new file (in soundfont's original format, such
 * as SF2).
 *
 * sf =			Handle returned by SfarkAlloc().
 * sfontName =	Nul-terminated name of the soundfont file, or
 *				0 if the original name is desired.
 *
 * RETURN: 0 = success, or negative error number.
 */

int WINAPI SfarkBeginExtract(SFARKHANDLE sf, const void * sfontName)
{
	register struct SFARKINFO *	sfarkInfo;
	register int				errCode;

	if ((sfarkInfo = (struct SFARKINFO *)sf) && (sfarkInfo->Flags & SFARK_IN1_OPEN))
	{
		// If app didn't specify a name, use the original sfont name (that SfarkOpen stored
		// in WorkBuffer2)
		if (!sfontName) sfontName = sfarkInfo->WorkBuffer2;

		if (sfarkInfo->CompressType == 2)
		{
			// For version 1, we need to extract two temp files from sfark in the first phase, then
			// recombine those 2 pieces into the soundfont. We saved the original sf name in the
			// 1024 bytes at the end of WorkBuffer2. Now let's also save the app's choice of sf
			// name at the end of WorkBuffer1
			sfarkInfo->u.v1.AppFontName = sfontName;
			if (sfontName != sfarkInfo->WorkBuffer2)
			{
				sfarkInfo->u.v1.AppFontName = (const char *)(sfarkInfo->WorkBuffer1 + 262144 - 1024);
#ifdef WIN32
				if (sfarkInfo->Flags & SFARK_UNICODE)
					lstrcpyW((WCHAR *)sfarkInfo->u.v1.AppFontName, (WCHAR *)sfontName);
				else
#endif
					lstrcpyA((char *)sfarkInfo->u.v1.AppFontName, (char *)sfontName);
			}

			// Now we need to create the intermediary "part file" in the temp dir. Its name will be
			// the orig soundfont name, with an extra '1' or '2' appended (depending on whether we're
			// extracting the 1st or 2nd part)
			getTempPartName(sfarkInfo, sfarkInfo->u.v1.PartNum);
			sfontName = sfarkInfo->WorkBuffer2;

			initV1vars(sfarkInfo);
		}
		else
		{
			// Init decomp routine vars
			sfarkInfo->RunningChksum = sfarkInfo->RunningUncompSize = sfarkInfo->BitRegister = sfarkInfo->NumBitsInRegister = sfarkInfo->Percent = 0;
			sfarkInfo->InputBufIndex = sizeof(sfarkInfo->InputBuf)/sizeof(uint16_t);
			ZeroMemory(&sfarkInfo->u.v2, sizeof(struct SFARKINFO_V2));
			sfarkInfo->u.v2.BitFramePackMethod = 8;
			sfarkInfo->u.v2.NumShortsInLpcBlock = 4096;

			switch (sfarkInfo->CompressType)
			{
				case 4:
				{
					sfarkInfo->u.v2.Unpack2EncodeLimit = 3;
					// sfarkInfo->u.v2.Unpack3EncodeLimit = 0;
					break;
				}

				case 5:
				{
					sfarkInfo->u.v2.Unpack2EncodeLimit = sfarkInfo->u.v2.Unpack3EncodeLimit = 20;
					sfarkInfo->u.v2.NumShortsInLpcBlock = 1024;
					break;
				}

				case 6:
				{
					sfarkInfo->u.v2.Unpack2EncodeLimit = sfarkInfo->u.v2.Unpack3EncodeLimit = 3;
					sfarkInfo->u.v2.NumShortsInLpcFrame = 8;
					break;
				}

				case 7:
				{
					sfarkInfo->u.v2.Unpack2EncodeLimit = 3;
					sfarkInfo->u.v2.Unpack3EncodeLimit = 5;
					sfarkInfo->u.v2.NumShortsInLpcFrame = 128;
				}
			}
		}

		// Open (create) the output (soundfont) file
		if (!(errCode = createSfFile(sfarkInfo, sfontName)))
		{
			sfarkInfo->Flags |= SFARK_OUT_OPEN;
			if (sfarkInfo->CompressType == 2) sfarkInfo->Flags |= SFARK_TEMP_MADE;

			// First V2 block always uses LZIP
			sfarkInfo->RunState = SFARK_LZIP;
		}

		return errCode;
	}

	return SFARKERR_APP;
}





/********************** loadSfarkHeader() *******************
 * Searches for, and loads (into SFARKINFO->WorkBuffer1), the
 * sfArk file's header.
 *
 * RETURN: 0 = success, or negative error number.
 *
 * Caller must account for little endian order, and
 * Ansi C filename, in the header.
 */

static int loadSfarkHeader(register struct SFARKINFO * sfarkInfo)
{
	unsigned char *	fileHdrStartPtr;
	unsigned char *	filename;
	unsigned int	bytesToProcess;
	int				result;
	unsigned int	headerSize;
	unsigned char	flags;

	flags = 0;

	bytesToProcess = 0;

	// Look for the sfArk ID (5 ASCII chars). If an sfArk file, this should be
	// located at byte offset of 26 within the sfArk file header
	for (;;)
	{
		// Do we need to read more bytes in order to check for the ID?
		while (bytesToProcess < 26 + 5)
		{
			MoveMemory(sfarkInfo->WorkBuffer1, fileHdrStartPtr, bytesToProcess);
			fileHdrStartPtr = sfarkInfo->WorkBuffer1;

			if (!(result = readSfarkFile(sfarkInfo, sfarkInfo->WorkBuffer1 + bytesToProcess, (44 + 256) - bytesToProcess)))
			{
bad2:			result = SFARKERR_NOID;
				if (flags & 0x01) result = SFARKERR_BADHDR;
				if (flags & 0x02) result = SFARKERR_OLDVERS;
				if (flags & 0x04) result = SFARKERR_UNKCOMP;
				if (flags & 0x08) result = SFARKERR_BADHDRSUM;
			}

			if (result < 0)
bad:			return result;

			bytesToProcess += (unsigned int)result;
			if (bytesToProcess < 26 + 5) goto bad2;
		}

		// Is the ID where we expect it?
		if (!memcmp(fileHdrStartPtr + 26, &SfArkId[1], sizeof(SfArkId) - 1))
		{
			// Move header to buffer start so it's DWORD aligned. Some CPUs need this, if we access a DWORD field
			if (fileHdrStartPtr != sfarkInfo->WorkBuffer1)
			{
				MoveMemory(sfarkInfo->WorkBuffer1, fileHdrStartPtr, bytesToProcess);
				fileHdrStartPtr = sfarkInfo->WorkBuffer1;
			}

			// Do we need to read more bytes in order to grab the header upto the filename? Upto filename is
			// 44 bytes. we also make sure we've read the first char of the filename in prep of parsing it below
			if (bytesToProcess < 45)
			{
				if ((result = readSfarkFile(sfarkInfo, sfarkInfo->WorkBuffer1 + bytesToProcess, (44 + 256) - bytesToProcess)) < 0) goto bad;
				bytesToProcess += (unsigned int)result;
				if (bytesToProcess < 45) goto bad2;
			}
			
			// We found a legit sfArk header
			flags |= 0x01;

			// Version 1 sfArk files have a CompressType < 3 (ie,2)
			if (fileHdrStartPtr[31] > 3)
			{	
				if (fileHdrStartPtr[31] <= 7)  break;
				flags |= 0x04;
			}

			// Check that it's version 1
			if (get_ulong(fileHdrStartPtr) == 0x04034c46 && fileHdrStartPtr[31] == 2)
			{
				// It's a V1 file
				flags |= 0x02;
				
				// We don't bother with decrypted files
				if (!(fileHdrStartPtr[7] & 0x40))
				{
					register unsigned int	slop;

					// Get the filename len
					headerSize = get_ushort(fileHdrStartPtr + 36);
					slop = get_ushort(fileHdrStartPtr + 38) + headerSize;

					// Make sure we read in the filename now (plus an additional str we ignore)
					if (bytesToProcess < 40 + slop)
					{
						if ((result = readSfarkFile(sfarkInfo, sfarkInfo->WorkBuffer1 + bytesToProcess, (slop + 40) - bytesToProcess)) < 0) goto bad;
						bytesToProcess += (unsigned int)result;
						if (bytesToProcess < 40 + slop) goto bad2;
					}

					// Copy certain fields to the equivolent fields in the V2 header
					MoveMemory(fileHdrStartPtr + 42, fileHdrStartPtr + 40, headerSize);

					// The Lspack entry we want has a filename that ends in ".sfArk$1" or ".sfArk$2"
					if (headerSize > 8 && !memcmp(&fileHdrStartPtr[headerSize + 42 - 8], &SfArkId[0], sizeof(SfArkId)))
					{
						fileHdrStartPtr[0] = fileHdrStartPtr[headerSize + 42 - 1];	// save whether part 1 or 2
						headerSize -= 8;
						fileHdrStartPtr[headerSize + 42] = 0;
						CopyMemory(fileHdrStartPtr + 4, fileHdrStartPtr + 22, 4);
						MoveMemory(fileHdrStartPtr + 12, fileHdrStartPtr + 14, 4);
						CopyMemory(fileHdrStartPtr + 38, fileHdrStartPtr + 18, 4);
						fileHdrStartPtr[34] = (unsigned char)(headerSize & 0xFF);
						fileHdrStartPtr[35] = (unsigned char)((headerSize >> 8) & 0xFF);
						fileHdrStartPtr[36] = fileHdrStartPtr[37] = 0;
						headerSize = 40 + slop;
						goto out;
					}
				}
			}
		}

		// Keep looking for the V2 header
more:	++fileHdrStartPtr;
		if (bytesToProcess) --bytesToProcess;
	}

	// Read in upto 256 ASCII chars of nul-terminated filename
	filename = fileHdrStartPtr + 42;
	do
	{
		// Read in more chars of filename?
		while ((unsigned int)(filename - fileHdrStartPtr) >= bytesToProcess)
		{
			if ((result = readSfarkFile(sfarkInfo, filename, (44 + 256) - bytesToProcess)) < 0) goto bad;
			if (!result) goto bad2;
			bytesToProcess += (unsigned int)result;
		}

	} while (*filename++);

	headerSize = (filename - fileHdrStartPtr);

	// Verify the header checksum (with the header checksum field zeroed, as that isn't included. But
	// first save that value so we can later compare it)
	flags |= 0x08;
	result = get_ulong(fileHdrStartPtr + 16);
	*((uint32_t *)fileHdrStartPtr + 4) = 0;
	if (adler32(0, fileHdrStartPtr, headerSize) != (uint32_t)result) goto more;

	// Set file position to the end of the header
out:
	result = 0;
	if ((bytesToProcess -= headerSize)) result = setSfarkFilePos(sfarkInfo, -((int)bytesToProcess));

	return result;
}





static void cleanFiles(register struct SFARKINFO * sfarkInfo)
{
	if (sfarkInfo->Flags & SFARK_OUT_OPEN) closeFile(sfarkInfo->OutputFileHandle);
	if (sfarkInfo->Flags & SFARK_IN1_OPEN) closeFile(sfarkInfo->InputFileHandle);
	if (sfarkInfo->Flags & SFARK_IN2_OPEN) closeFile(sfarkInfo->u.v1.PartFileHandle);
	if (sfarkInfo->Flags & SFARK_TEMP_MADE)
	{
		getTempPartName(sfarkInfo, '2');
		DeleteFileA(sfarkInfo->WorkBuffer2);
		getTempPartName(sfarkInfo, '1');
		DeleteFileA(sfarkInfo->WorkBuffer2);
	}

	sfarkInfo->Flags &= ~(SFARK_OUT_OPEN|SFARK_IN1_OPEN|SFARK_IN2_OPEN|SFARK_TEMP_MADE);
	sfarkInfo->RunState = 0;
}





/******************** skipEmbeddedText() *******************
 * Skips over the embedded (compressed) text in the sfArk file
 * at the current position.
 *
 * NOTE: Must not alter WorkBuffer1 or WorkBuffer2.
 */

static int skipEmbeddedText(struct SFARKINFO * sfarkInfo)
{
	register int			result;
	register uint32_t		size;
	unsigned char			buf[4];

	// Read the compressed size (uint32_t) of text
	if ((result = readSfarkFile(sfarkInfo, buf, 4)) != 4 || (size = get_ulong(buf)) > 262144)
	{
		if (result >= 0)
			result = SFARKERR_BADLEN;
	}

	// Just skip over it
	else
		result = setSfarkFilePos(sfarkInfo, size);

	return result;
}





/*********************** SfarkOpen() **********************
 * Opens an sfArk file.
 *
 * sf =			Handle returned by SfarkAlloc().
 * sfarkName =	Nul-terminated name of the sfArk file.
 *
 * RETURN: 0 = success, or negative error number.
 */

int WINAPI SfarkOpen(SFARKHANDLE sf, const void * sfarkName)
{
	register struct SFARKINFO *	sfarkInfo;
	register int				errCode;

	if ((sfarkInfo = (struct SFARKINFO *)sf))
	{
		// Close any prev open files
		cleanFiles(sfarkInfo);

		// Open sfArk file
		if (!(errCode = openSfarkFile(sfarkInfo, sfarkName)))
		{
			// Read the sfArk header into WorkBuffer1
			if (!(errCode = loadSfarkHeader(sfarkInfo)))
			{
				// Copy over header to SFARKINFO. Note: int16/int32 data is stored in little
				// endian (80x86) order, so adjust if this system uses big endian
				sfarkInfo->FileUncompSize = get_ulong(sfarkInfo->WorkBuffer1 + 4);
				sfarkInfo->FileChksum = get_ulong(sfarkInfo->WorkBuffer1 + 12);
				sfarkInfo->CompressType = sfarkInfo->WorkBuffer1[31];
				sfarkInfo->LeadingPadUncompSize = get_ulong(sfarkInfo->WorkBuffer1 + 34);
				sfarkInfo->BitPackEndOffset = get_ulong(sfarkInfo->WorkBuffer1 + 38);

				// Copy the original sound name to WorkBuffer2, in preparation of the
				// app passing no filename to SfarkBeginExtract()
				lstrcpyA((char *)sfarkInfo->WorkBuffer2, (const char *)sfarkInfo->WorkBuffer1 + 42);
				
				// Vers 1
				if (sfarkInfo->CompressType == 2)
				{
					// For version 1, we need to extract two temp files from sfark in the first phase, then
					// recombine those 2 pieces into the soundfont. We need to save the original sf name
					// somewhere permanent so let's use the 1024 bytes at the end of WorkBuffer2 since it's much
					// larger than we need and won't be overwritten
					lstrcpyA((char *)sfarkInfo->WorkBuffer2 + 262144 - 1024, (const char *)sfarkInfo->WorkBuffer1 + 42);

					// Note the part file we're extracting first. Could be '1' or '2'
					sfarkInfo->u.v1.PartNum = sfarkInfo->WorkBuffer1[0];
				}
				else
				{
					// See if there's an embedded readme and/or license file. If so, skip over them. If someone
					// is stupid enough to use the undocumented, proprietary sfArk format to compress his
					// soundfont, just to save a few hundred bytes over open standards like zip, then we don't
					// care about the idiot's docs
					if (sfarkInfo->WorkBuffer1[0] & 2) errCode = skipEmbeddedText(sfarkInfo);
					if ((sfarkInfo->WorkBuffer1[0] & 1) && !errCode) errCode = skipEmbeddedText(sfarkInfo);
				}
			}

			if (errCode)
				closeFile(sfarkInfo->InputFileHandle);
			else
				sfarkInfo->Flags |= SFARK_IN1_OPEN;
		}
		
		return errCode;
	}

	return SFARKERR_APP;
}





/*********************** SfarkClose() **********************
 * Closes an sfArk file.
 *
 * sf =			Handle returned by SfarkAlloc().
 *
 * NOTE: Implicitly called by SfarkOpen. Also called by
 * SfarkBeginExtract or SfarkExtract if they return
 * non-zero.
 */

void WINAPI SfarkClose(SFARKHANDLE sf)
{
	register struct SFARKINFO *	sfarkInfo;

	if ((sfarkInfo = (struct SFARKINFO *)sf))
		cleanFiles(sfarkInfo);
}





/********************* SfarkPercent() ********************
 * Returns the completion percentage of the file currently
 * being extracted, on a scale of 0 to 10.
 */

unsigned int WINAPI SfarkPercent(SFARKHANDLE sf)
{
	register struct SFARKINFO *	sfarkInfo;

	sfarkInfo = (struct SFARKINFO *)sf;

	return sfarkInfo->Percent;
}				





/******************** SfarkGetBuffer() *******************
 * Returns a buffer that can be used prior to
 * SfarkBeginExtract(). If SfarkOpen() has been called,
 * the name of the original soundfont is copied to the
 * buffer (minus the path).
 *
 * RETURN: Pointer to a buffer.
 *
 * NOTE: App can alter the buffer.
 */

void * WINAPI SfarkGetBuffer(SFARKHANDLE sf)
{
	register struct SFARKINFO *	sfarkInfo;

	sfarkInfo = (struct SFARKINFO *)sf;

	if (sfarkInfo->Flags & SFARK_IN1_OPEN)
	{
		register const char	*str;

		str = (char *)sfarkInfo->WorkBuffer2;
#ifdef WIN32
		if (sfarkInfo->Flags & SFARK_UNICODE)
		{
			register WCHAR 		*dest;

			dest = (WCHAR *)sfarkInfo->WorkBuffer1;
			while ((*dest++ = (WCHAR)((unsigned char)*str++)));
		}
		else
#endif
			lstrcpyA((char *)sfarkInfo->WorkBuffer1, str);
	}

	return sfarkInfo->WorkBuffer1;
}






/******************** SfarkAlloc() *******************
 * Allocates an SFARKHANDLE. Must be called first before
 * calling any other functions.
 *
 * RETURN: An SFARKHANDLE if success, or 0 if not enough mem.
 */

void * WINAPI SfarkAlloc(void)
{
	register struct SFARKINFO *	sfarkInfo;

	if ((sfarkInfo = (struct SFARKINFO *)GlobalAlloc(GMEM_FIXED, sizeof(struct SFARKINFO))))
	{
		ZeroMemory(sfarkInfo, sizeof(struct SFARKINFO));

		// Get buffers needed for in-memory decompression. The largest "compressed block"
		// in an sfArk V2 file decompresses to 262144 bytes max
		sfarkInfo->WorkBuffer1 = (unsigned char *)GlobalAlloc(GMEM_FIXED, 262144);
		sfarkInfo->WorkBuffer2 = (unsigned char *)GlobalAlloc(GMEM_FIXED, 262144);
		if (!sfarkInfo->WorkBuffer1 || !sfarkInfo->WorkBuffer2)
		{
			if (sfarkInfo->WorkBuffer1) GlobalFree(sfarkInfo->WorkBuffer1);
			GlobalFree(sfarkInfo);
			sfarkInfo = 0;
		}
	}

	return sfarkInfo;
}





/******************** SfarkFree() *******************
 * Frees an SFARKHANDLE. Must be called last.
 */

void WINAPI SfarkFree(SFARKHANDLE sf)
{
	register struct SFARKINFO *	sfarkInfo;

	if ((sfarkInfo = (struct SFARKINFO *)sf))
	{
		cleanFiles(sfarkInfo);
		GlobalFree(sfarkInfo->WorkBuffer2);
		GlobalFree(sfarkInfo->WorkBuffer1);
		GlobalFree(sfarkInfo);
	}
}





/******************** SfarkErrMsg() *******************
 * Returns an error message for the specified SFARK
 * error number returned by any other functions.
 *
 * RETURN: Pointer to a nul-terminated msg.
 */

void * WINAPI SfarkErrMsg(SFARKHANDLE sf, int code)
{
	register const char	*str;
	register struct SFARKINFO *	sfarkInfo;

	str = &ErrorMsgs[0];
	while (code++ && *str) str += (lstrlenA(str) + 1);
	if (!(*str)) str = &UnknownErr[0];

	if ((sfarkInfo = (struct SFARKINFO *)sf))
	{
#ifdef WIN32
		if (sfarkInfo->Flags & SFARK_UNICODE)
		{
			register WCHAR 		*dest;

			dest = (WCHAR *)sfarkInfo->WorkBuffer1;
			while ((*dest++ = (WCHAR)((unsigned char)*str++)));
		}
		else
#endif
			lstrcpyA((char *)sfarkInfo->WorkBuffer1, str);

		return sfarkInfo->WorkBuffer1;
	}

	return (void *)str;
}
