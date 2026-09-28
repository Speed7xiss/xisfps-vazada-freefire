#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <Windows.h>
#include <cstdint>
#include <iostream>
#include <algorithm>
#include <memory>
#include <locale>
#include <cwctype>
#include <filesystem>
#include <thread>
#include <chrono>

#include <tchar.h>

// --- Conteúdo de Compress.hpp ---

typedef void( __stdcall* __RtlZeroMemory )( void* , size_t );
typedef void( __stdcall* __RtlMoveMemory )( void* , const void* , size_t );
typedef DWORD( __stdcall* __RtlComputeCrc32 )( DWORD , const BYTE* , INT );
typedef NTSTATUS( __stdcall* __RtlDecompressBufferEx )( USHORT , PUCHAR , ULONG , PUCHAR , ULONG , PULONG , PVOID );
typedef WCHAR( __stdcall* __RtlUpcaseUnicodeChar )( WCHAR );
typedef NTSTATUS( __stdcall* __RtlGetCompressionWorkSpaceSize )( USHORT , PULONG , PULONG );
typedef ULONG( __stdcall* __RtlNtStatusToDosError )( NTSTATUS );
typedef NTSTATUS( __stdcall* __RtlCompressBuffer )( USHORT , PUCHAR , ULONG , PUCHAR , ULONG , ULONG , PULONG , PVOID );

static HMODULE hNtDll = 0;
static __RtlComputeCrc32 _RtlComputeCrc32 = 0;
static __RtlUpcaseUnicodeChar _RtlUpcaseUnicodeChar = 0;
static __RtlDecompressBufferEx _RtlDecompressBufferEx = 0;
static __RtlCompressBuffer _RtlCompressBuffer = 0;
static __RtlGetCompressionWorkSpaceSize _RtlGetCompressionWorkSpaceSize = 0;
static __RtlNtStatusToDosError _RtlNtStatusToDosError = 0;
static __RtlMoveMemory _RtlMoveMemory = 0;
static __RtlZeroMemory _RtlZeroMemory = 0;

BOOL IsFileCompressed( BYTE* Data ) { return *Data == 0x4D; }

DWORD CalculChecksum( BYTE* Input , DWORD InputSize ) {
	if ( !_RtlComputeCrc32 ) _RtlComputeCrc32 = ( __RtlComputeCrc32 ) GetProcAddress( hNtDll , "RtlComputeCrc32" );
	if ( !_RtlComputeCrc32 ) return 0;
	return _RtlComputeCrc32( 0 , Input , InputSize );
}

UCHAR* BufferCompress( UCHAR* input , ULONG inputSize , UCHAR* output , ULONG outputSize , ULONG* FinaloutputSize ) {
	ULONG dummy = 0 , UncompressedChunkSize = 0 , WorkSpaceSize = 0 , WorkFragSize = 0;
	PVOID WorkSpace = nullptr;
	NTSTATUS Status = 0;

	if ( !hNtDll && !( hNtDll = LoadLibrary( _T( "ntdll.dll" ) ) ) ) return nullptr;
	if ( !_RtlCompressBuffer ) {
		_RtlCompressBuffer = ( __RtlCompressBuffer ) GetProcAddress( hNtDll , "RtlCompressBuffer" );
		_RtlGetCompressionWorkSpaceSize = ( __RtlGetCompressionWorkSpaceSize ) GetProcAddress( hNtDll , "RtlGetCompressionWorkSpaceSize" );
		if ( !_RtlCompressBuffer || !_RtlGetCompressionWorkSpaceSize ) return nullptr;
	}

	Status = _RtlGetCompressionWorkSpaceSize( COMPRESSION_FORMAT_XPRESS_HUFF , &WorkSpaceSize , &WorkFragSize );
	if ( Status != ERROR_SUCCESS ) return nullptr;

	WorkSpace = malloc( WorkSpaceSize );
	Status = _RtlCompressBuffer( COMPRESSION_FORMAT_XPRESS_HUFF , input , inputSize , output , outputSize , UncompressedChunkSize , &dummy , WorkSpace );
	if ( Status != ERROR_SUCCESS ) return nullptr;

	*FinaloutputSize = dummy;
	if ( WorkSpace ) free( WorkSpace );
	if ( hNtDll ) FreeLibrary( hNtDll );
	return output;
}

DWORD Addat( DWORD Offset , BYTE** Input , DWORD* InputSize , DWORD* ToAdd ) {
	DWORD SizeOutput = *InputSize + 4;
	BYTE* output = ( BYTE* ) calloc( SizeOutput , 1 );
	if ( !output ) return GetLastError( );

	*( DWORD* ) output = *ToAdd;
	if ( memcpy_s( output + 4 , SizeOutput , *Input , *InputSize ) ) return GetLastError( );

	*Input = output;
	*InputSize = SizeOutput;
	return 0;
}

bool CompressAndSave( const std::string& path , std::vector<char>& DataDecomp ) {
	BYTE* DataComp = nullptr;
	DWORD DataComp_Size = 0 , nbBytesWritten = 0;
	DWORD ToAdd = 0;

	DataComp = ( BYTE* ) calloc( DataDecomp.size( ) * 2 , 1 );
	if ( !DataComp ) return false;

	DataComp = BufferCompress( reinterpret_cast< BYTE* >( DataDecomp.data( ) ) ,
		static_cast< DWORD >( DataDecomp.size( ) ) ,
		DataComp ,
		static_cast< DWORD >( DataDecomp.size( ) ) * 2 ,
		&DataComp_Size );
	if ( !DataComp ) { free( DataComp ); return false; }

	ToAdd = 72171853;
	DWORD decompressedSize = static_cast< DWORD >( DataDecomp.size( ) );
	if ( Addat( 0 , &DataComp , &DataComp_Size , &decompressedSize ) ||
		Addat( 0 , &DataComp , &DataComp_Size , &ToAdd ) ) {
		free( DataComp );
		return false;
	}

	HANDLE hFile = CreateFileA( path.c_str( ) , GENERIC_WRITE , 0 , NULL , CREATE_ALWAYS , FILE_ATTRIBUTE_NORMAL , NULL );
	if ( hFile == INVALID_HANDLE_VALUE ) { free( DataComp ); return false; }

	WriteFile( hFile , DataComp , DataComp_Size , &nbBytesWritten , NULL );
	CloseHandle( hFile );
	free( DataComp );
	return true;
}