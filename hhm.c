/*
hhm -- make an ITS file and in the future a CHM file
Copyright (C) 2002 Pabs

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 only

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software Foundation,
Inc., 59 Temple Place, Suite 330, Boston, MA, 02111-1307, USA
*/



/*
Before we begin:

Any patches must include error handling of some kind where appropriate
or have comments on why the error handling is unnessecary. Don't bother
checking free or fclose though.

Be careful about not using octal (0777 != 777)

When reallocing use the size, len framework
	size_t new_x_len = x_len + 1; // For example
	size_t new_x_size = ((new_x_len/x_grow)+1)*x_grow;
	if( new_x_size > x_size ){
		x_t* new_x = (x_t*)realloc(x,sizeof(x_t)*new_x_size);
		if( !new_x ){
			return;
		}
		x = new_x;
		x_size = new_x_size;
	}
	make_x(&x[x_len]);
	x_len = new_x_len;

When free()ing use the FREE() macro, unless you are free()ing a
non-returned local variable, in which case just use free().

Portability:

Any patches must be endian-neutral - that is they must work on big-endian
processors.

Any patches must be int size-neutral - that is they must work on processors
that have large or small registers. Usage on small int processors should be
achieved using compiler emulation of larger ints.
*/



/* General FIXMEs */
/* Make hhm more general with respect to multiple content sections */



/* System headers */

#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
//#include <sys/param.h>



/* LZX headers */

/* cygwin doesn't define these */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

#if !(defined(__CYGWIN__))
/* Get this in sys/types.h thru dirent.h on cygwin */
/* Matthew tells me it needs to be defined on Linux */
typedef unsigned int uint;
#endif

#include "lzx_compress.h"
#include "lzx_constants.h"



#define PROGNAME "hhm"
#define VERSION "0.1"



/* Some convenience stuff */

#define FREE(a) free(a); (a) = NULL

typedef uint8_t BYTE; /* 1-byte integer */
typedef uint16_t WORD; /* 2-byte integer */
typedef uint32_t DWORD; /* 4-byte integer */
typedef uint64_t QWORD; /* 8-byte integer */

typedef char bool;
#define true 1
#define false 0

typedef DWORD ENCINT; /* Encoded integer base type. This can be increased in size if need be to support huge files */

/* FIXME: Find optima for different platforms */
#define FCAT_BUFFER_SIZE 512 /* Used by fcat for a temporary buffer */



typedef DWORD LCID;

/* FIXME: Find out what Wine (the emulator :-) defines */
#if defined(WIN32) || defined(__CYGWIN__) /* || defined(WINE) */
/* Windows locale functions */
#define WIN32_LCID
#define WIN32_TIMESTAMP
/* Fuck including all those Win32 headers for only 2 functions */
#define WINAPI __stdcall
/*
Don't need to link with anything special to get these
because they come from libkernel32.a/kernel32.lib,
which is linked in by default on Win32 platforms
*/
LCID WINAPI GetUserDefaultLCID(void);
typedef struct _FILETIME {
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME,* LPFILETIME;
typedef void* HANDLE;
DWORD WINAPI GetFileTime(HANDLE fh,LPFILETIME ctime,LPFILETIME atime,LPFILETIME mtime);
#else
/* FIXME: Locale functions for Unix/MacOS/Win16/VMS/...blah */
#endif



/* Options/Preferences */

/* All the options below are set to what MS' implementation uses */

DWORD hri = 1; /* Huffman reset interval  in 0x8000 byte blocks */
DWORD wsc = 1; /* Window size code (ws in 0x8000 byte blocks) */
DWORD ws = 15; /* Window size used by compressor */
DWORD ihv = 3; /* Initial header version */
DWORD iht = 0; /* Initial header timestamp */
DWORD dhv = 1; /* Directory header version */
DWORD rtv = 2; /* ResetTable version */
DWORD dcs = 0x1000; /* Directory chunk size */
DWORD qrd = 2; /* Quickref density */
WORD tqd; /* True quickref density */
/* FIXME: until we can get it from the OS */
LCID ihl = 0x409 /* en-us */; /* Initial header language */
/* FIXME: until we know more about it */
LCID dhl = 0x409 /* en-us */; /* Directory header language */
/* Transform/List is buggy by default for maximum compatibility */
/*FIXME: Work out if it is actually used by any decompilers */
bool tlb = true; /* Transform/List buggy? */
bool atl = true; /* Add Transform/List? */
/* adding dirs bloats the file, but is done by default by HHC */
bool ad = true; /* Add dirs? */
/* adding paths bloats the file, but is done by default by HHC */
bool flat = false; /* Add pathnames? */
/* Zeroing out free space takes a tiny-weeny-eeny bit more time, but makes the resulting file easier to compress */
#ifdef DEBUG
bool zfs = true; /* Zero free space? */
#else
bool zfs = false; /* Zero free space? */
#endif
bool sub = true; /* Subdivide frames? */
char def_cs = 1; /* Default content section for author files */
bool rm_output = true; /* remove() the output file on exit()? */



#define _grow 10

/* Output file stuff */

char* output_file = NULL;
size_t output_len = 0;
FILE* output_file_h = NULL;

/* File item management */

/* The current sub-folder */
char* prefix = NULL;
uint prefix_size = 0;
uint prefix_len = 0;
#define prefix_grow _grow

/* These are for item::flags */
/* Don't free */
#define F_DONT_FREE 1
/* FIXME: Allow hard linked files */
#define F_HL 2

typedef struct _item_t {
	BYTE name_len;
	char* name;
	ENCINT cs;
	ENCINT offset;
	ENCINT length;
	BYTE flags;
} item_t;

item_t** files = NULL;
size_t files_size = 0;
size_t files_len = 0;
#define files_grow _grow

uint current_offset = 0;
uint current_len = 0;
uint current_file_i = 0;
FILE* current_file = NULL;

/* Reset table stuff */

DWORD num_frames = 0;

QWORD* reset_table = NULL;
size_t reset_table_size = 0;
size_t reset_table_len = 0;
#define reset_table_grow _grow

#if 0
/* Differentially storing files in different sections */

/* FIXME: Implement storing files in content section 0 */
uint* cs0_files = NULL;
size_t cs0_files_size = 0;
size_t cs0_files_len = 0;
#define cs0_files_grow _grow

uint* cs1_files = NULL;
size_t cs1_files_size = 0;
size_t cs1_files_len = 0;
#define cs1_files_grow _grow
#endif

/* Directory chunk stuff */

typedef struct _chunk_t {
	BYTE* chunk;
	DWORD space_left;
	BYTE* current_offset;
	BYTE* current_quickref;
	WORD num_entries;
	uint current_index;
	uint first_file_index;
} chunk_t;

DWORD prev_chunk = -1;
DWORD last_listing_chunk = 0;

chunk_t* dir_chunks = NULL;
size_t dir_chunks_size = 0;
size_t dir_chunks_len = 0;
/*
Only itsf's with freaking huge numbers of files or with tiny directory
chunks will need more than 4 levels of directory chunks (1 listing, 3
index) The biggest seen so far (KB.CHM from the MSVC6 MSDN is 116,512,980
bytes with 512 directory chunks) only has 2 index levels.
*/
#define dir_chunks_grow 4



/* Some constants */

/* GUIDs embedded in the file */

typedef struct _GUID {
	DWORD Data1;
	WORD Data2;
	WORD Data3;
	BYTE Data4[8];
} GUID;

/* FIXME: Convert these to BYTE arrays */
GUID ihg0 = {0x7C01FD10,0x7BAA,0x11D0,{0x9E,0x0C,0x00,0xA0,0xC9,0x22,0xE6,0xEC}}; /* Initial header GUID 0 */
GUID ihg1 = {0x7C01FD11,0x7BAA,0x11D0,{0x9E,0x0C,0x00,0xA0,0xC9,0x22,0xE6,0xEC}}; /* Initial header GUID 1 */
GUID dhg =  {0x5D02926A,0x212E,0x11D0,{0x9D,0xF9,0x00,0xA0,0xC9,0x22,0xE6,0xEC}}; /* Directory header GUID */

/*
NameList file
WORD 30;
WORD 2;
WORD 12;
wchar L"Uncompressed"
WORD 12;
wchar L"MSCompressed"
*/
#define nlfs 60
BYTE nlf[nlfs] =
"\x1e\0"
"\2\0"
"\xc\0"
"\x55\0\x6e\0\x63\0\x6f\0\x6d\0\x70\0\x72\0\x65\0\x73\0\x73\0\x65\0\x64\0\0\0"
"\xc\0"
"\x4d\0\x53\0\x43\0\x6f\0\x6d\0\x70\0\x72\0\x65\0\x73\0\x73\0\x65\0\x64\0\0\0";

/*
Transform/List file
Buggy version: wchar L"{7FC28940-9D31-11D0"
Non-buggy version: char "{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}"
*/
#define tlfs 38
BYTE tlf[tlfs] = "\x7b\x37\x46\x43\x32\x38\x39\x34\x30\x2d\x39\x44\x33\x31\x2d\x31\x31\x44\x30\x2d\x39\x42\x32\x37\x2d\x30\x30\x41\x30\x43\x39\x31\x45\x39\x43\x37\x43\x7d";
BYTE tlfb[tlfs] = "\x7b\0\x37\0\x46\0\x43\0\x32\0\x38\0\x39\0\x34\0\x30\0\x2d\0\x39\0\x44\0\x33\0\x31\0\x2d\0\x31\0\x31\0\x44\0\x30\0";

/* ControlData, SpanInfo files */
#define cdfs 28
#define sifs 8



/* Directory information for some predefined files */
/*
They are written below in the order of offset that HHW output them in version:
HTML Help Workshop: 4.74.8702.0
HTML Help Image Editor: 4.74.8702.0
HHA: 4.74.8702.0
ITCC: 4.72.7277.0
HHCTRL: 4.74.8875.0
ITIRCL: 4.72.7277.0
ITSS: 4.72.8085.0
FIXME: Monitor future versions of HHW/HHC/HHA to make sure the output order doesn't change
FIXME: Check older versions of HHW/HHC/HHA & allow HHM to use the same output order
*/
/* All the directories & empty files go here (len, "name", 0, 0, 0) */
item_t InstanceData_item = { 95, "::DataSpace/Storage/MSCompressed/Transform/{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/", 0, 0, 0, F_DONT_FREE };
/* Now the non-empty files */
item_t NameList_item = { 20, "::DataSpace/NameList", 0, 0, nlfs, F_DONT_FREE };
item_t Transform_List_item = { 47, "::DataSpace/Storage/MSCompressed/Transform/List", 0, nlfs, tlfs, F_DONT_FREE };
item_t SpanInfo_item = { 41, "::DataSpace/Storage/MSCompressed/SpanInfo", 0, nlfs+tlfs, sifs, F_DONT_FREE };
item_t ControlData_item = { 44, "::DataSpace/Storage/MSCompressed/ControlData", 0, nlfs+tlfs+sifs, cdfs, F_DONT_FREE };
item_t ResetTable_item = { 105, "::DataSpace/Storage/MSCompressed/Transform/{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/ResetTable", 0, nlfs+tlfs+sifs+cdfs, 0, F_DONT_FREE }; /* Last 1 will be updated */
/* The cs 0 author & internal files go here */
item_t Content_item = { 40, "::DataSpace/Storage/MSCompressed/Content", 0, 0, 0, F_DONT_FREE }; /* Last 2 will be updated */



/* Protoypes below are in mostly order of usage */

/* In core directory item management prototypes */

static int add_dir_contents(char*d);
static int add_dir( void );
static int add_file( char* file );
static int add_item( item_t* new_item );
static int compare_items( const void* arg1, const void* arg2 );
static void sort_items( void );



/* LZX callback prototypes */

static int get_bytes( void* arg, int n, void* buf );
static int put_bytes( void* arg, int n, void* buf );
static void mark_frame( void* arg, DWORD uncomp, DWORD comp );
static int at_eof( void* arg );



/* Binary output prototypes */

static int fwrite_WORD( FILE* f, const WORD i );
static int fwrite_DWORD( FILE* f, const DWORD i );
static int fwrite_QWORD( FILE* f, const QWORD i );
static int fwrite_GUID( FILE* f, const GUID* i );



/* Directory output helper functions */

static void malloc_level( uint level );
static void init_level( uint level );
static void deinit_level( uint level );
static void init_index_chunk( uint level );
static void init_listing_chunk( void );
static uint add_item_to_chunk( uint level, item_t* item, ENCINT listing_chunk );



/* ENCINT helper functions */

static uint enc_int_len( ENCINT ei );
static uint write_enc_int( BYTE* buf, ENCINT ei );



/* Data prototypes */

static int fcat( FILE* sink, FILE* source );



/* Misc. prototypes */

static void usage( void );
static void on_exit( void );
static void free_all( void );



/* The entry point */

/* 0 success, -1 failure for now */
int main( int argc, char* argv[] ){
	char* input;
	struct stat input_info;
	FILE* f; /* alias for output_file_h */
	uint i;
	int err = 0;

	/* FIXME: Add options processing */
	if ( argc != 2 ) {
		usage();
		return -1;
	}

	/* Fix up some stuff */
	tqd = 1 + (1 << qrd); /* How often to spit out a quickref entry */
#ifdef WIN32_LCID
	ihl = GetUserDefaultLCID();
#else
	/* FIXME: Learn how to get the right LCID from Unix */
#endif
	if( !atl ){
		SpanInfo_item.offset -= tlfs;
		ControlData_item.offset -= tlfs;
		ResetTable_item.offset -= tlfs;
	}
	switch(wsc) {
		case 1: ws = 15; break;
		case 2: ws = 16; break;
		case 4: ws = 17; break;
		case 8: ws = 18; break;
		case 0x10: ws = 19; break;
		case 0x20: ws = 20; break;
		case 0x40: ws = 21; break;
		default:
			printf( "ERROR: Invalid window size code: 0x%x (must be one of 1, 2, 4, 8, 0x10, 0x20 or 0x40)\n", wsc );
		return -1;
	}

	input = argv[1];

	if( stat( input, &input_info ) != 0 ){
		printf( "ERROR: Could not find the input dir or .hhp file\n");
		return -1;
	}

	/* Delete all data on the heap on exit */
	atexit(on_exit);

	if( S_ISDIR(input_info.st_mode) ){
		prefix = (char*)malloc(2);
		if(!prefix) return -1;
		prefix[0] = '/';
		prefix[1] = '\0';
		prefix_len = 1;
		if( ad && add_dir() == -1 ){
			printf( "WARNING: Could not add the root dir of the chm\n" );
		}
		add_dir_contents(input);
		/* Don't know why anyone would want an empty ITS, but whatever */
		if(!files_len) printf("WARNING: The input directory was empty (no files added)\n");
	} else {
		/* FIXME: Process HHP & add internal files */
		printf( "ERROR: CHM output has not yet been implemented (feel free to contribute)\n");
		return -1;
	}

	/* Add the format-related files */
	add_item( &NameList_item );
	add_item( &Content_item );
	add_item( &ControlData_item );
	add_item( &SpanInfo_item );
	if( atl ) add_item( &Transform_List_item );
	if( ad ) add_item( &InstanceData_item );
	add_item( &ResetTable_item );

	sort_items();

	/* FIXME: When we get HHP processing this will need to be changed */
	output_len = strlen(input);
	/* Shell auto-completion may provide an undesirable '/' at the end of an argument dear liza */
	if( input[output_len-1] == '/' ) input[--output_len] = '\0'; /* So fix it dear henry */
	output_file = (char*)malloc(output_len+sizeof(".its"));
	if(output_file){
		strcpy(output_file,input);
		strcpy(output_file+output_len,".its");

		f = output_file_h = fopen(output_file,"wb");
		if(f){
			FILE* tf;
			/* Get the initial header timestamp */
			int fd = fileno(f);
			{
#ifdef WIN32_TIMESTAMP
				FILETIME last_write_time;
				HANDLE fh = (HANDLE) _get_osfhandle(fd);
				if( GetFileTime(fh, NULL, NULL, &last_write_time) ){
					/* I guess someone was a fan of the hitchhikers guide to the galaxy! */
					iht = last_write_time.dwLowDateTime + 42;
				} else printf( "WARNING: Could not get information for the initial header timestamp (using zero instead)\n" );
#else
				struct stat info;
				if( ! fstat( fd, &info ) ){
					/* I guess someone was a fan of the hitchhikers guide to the galaxy! */
					/* FIXME: This is not very accurate on Win32, should it be used? */
					iht = (DWORD)( (QWORD)info.st_mtime * (QWORD)100000000 + (QWORD)116444736000000000 ) + 42;
				} else printf( "WARNING: Could not get information for the initial header timestamp (using zero instead)\n" );
#endif
			}
			/*
			We crunch all the input to a temp file first
			FIXME: Figure out a way to do this last?
			*/
			tf = tmpfile();
			if(tf){
				lzx_data* lzxd;
				lzx_results lzxr;

				if(!lzx_init(&lzxd, ws, get_bytes, NULL, at_eof, put_bytes, tf, mark_frame, NULL)){

					DWORD last_listing_chunk = 0;
					long content_offset;
					int block_size = 1 << ws;

					chdir( input );

					while( !at_eof( NULL ) ){
						lzx_reset( lzxd );
						if( lzx_compress_block( lzxd, block_size, sub ) < 0 ){
							printf( "ERROR: LZX compression failure\n" );
							break;
						}
					}

					chdir( ".." );

					if( lzx_finish( lzxd, &lzxr ) < 0 ){
						printf( "ERROR: LZX deinitialisation failure\n" );
						exit( -1 );
					}

					/* Don't close tf yet since we still need the data */

					if( feof(tf) || ferror(tf) ){
						printf("ERROR: Temporary file write error\n");
						exit(-1);
					}

					/* Now we have the results we can update some of the directory item info */
					ResetTable_item.length = 48+8*reset_table_len;
					Content_item.offset = ResetTable_item.offset + ResetTable_item.length;
					Content_item.length = lzxr.len_compressed_output;

					/*
					We don't check the output of all the fwrite()s because:
						we can check it later using ferror
						the time to exit on failure will be small anyway
					*/

					{ /* Output the initial header */
						char sig[4] = "ITSF";
						fwrite(sig,4,1,f);
						fwrite_DWORD( f, ihv );
						fwrite_DWORD( f, ihv == 2 ? 0x58 : 0x60 );
						fwrite_DWORD( f, 1 );
						fwrite_DWORD( f, iht );
						fwrite_DWORD( f, ihl );
						fwrite_GUID( f, &ihg0 );
						fwrite_GUID( f, &ihg1 );
					}

					{ /* Output the header section table */
						fwrite_QWORD( f, 96 );
						fwrite_QWORD( f, 24 );
						fwrite_QWORD( f, 120 );
						fseek( f, 8, SEEK_CUR ); /* We'll have to come back to this QWORD */
					}

					/* Output the content section offset */
					if( ihv == 3 ){
						fseek( f, 8, SEEK_CUR ); /* We'll have to come back to this QWORD */
					}

					{ /* Output header section 0 */
						fwrite_DWORD( f, 0x1fe );
						fwrite_DWORD( f, 0 );
						fseek( f, 8, SEEK_CUR ); /* We'll have to come back to this QWORD */
						fwrite_DWORD( f, 0 );
						fwrite_DWORD( f, 0 );
					}

					{ /* Output header section 1 header (directory header) */
						char sig[4] = "ITSP";
						fwrite(sig,4,1,f);
						fwrite_DWORD( f, dhv );
						fwrite_DWORD( f, 0x54 );
						fwrite_DWORD( f, 0xa );
						fwrite_DWORD( f, dcs );
						fwrite_DWORD( f, qrd );
						fseek( f, 8, SEEK_CUR ); /* We'll have to come back to these two DWORDs */
						fwrite_DWORD( f, 0 );
						fseek( f, 4, SEEK_CUR ); /* We'll have to come back to this DWORD */
						fwrite_DWORD( f, 0xffffffff );
						fseek( f, 4, SEEK_CUR ); /* We'll have to come back to this DWORD */
						fwrite_DWORD( f, dhl );
						fwrite_GUID( f, &dhg );
						fwrite_DWORD( f, 0x54 );
						fwrite_DWORD( f, 0xffffffff );
						fwrite_DWORD( f, 0xffffffff );
						fwrite_DWORD( f, 0xffffffff );
					}

					{ /* Output directory */
						long num_chunks_to_rewind;
						bool full, coming_down = false;
						size_t level = dir_chunks_len = 0;
						current_file_i = 0;
						malloc_level( 0 );
						init_listing_chunk( );
						dir_chunks[0].first_file_index = 0;
						do{
							if( coming_down && !level ){
								fseek( f, -(dcs*num_chunks_to_rewind)+0x10, SEEK_CUR );
								fwrite_DWORD( f, prev_chunk+1 );
								fseek( f, dcs*num_chunks_to_rewind-0x14, SEEK_CUR );
							}
							coming_down = false;
							if( level >= dir_chunks_len ){
								malloc_level( level );
								init_level( level );
							}
							if( !dir_chunks[level].current_index ){
								if( level )
									add_item_to_chunk( level, files[dir_chunks[level-1].first_file_index], 0 );
								dir_chunks[level].first_file_index = current_file_i;
							}
							full = add_item_to_chunk( level, files[current_file_i], prev_chunk+1 );
							if( full ){
								deinit_level( level );
								prev_chunk++;
								if( level ) num_chunks_to_rewind++;
								else last_listing_chunk = prev_chunk;
								init_level( level );
								level++;
							} else if( level ){
								num_chunks_to_rewind = 1;
								coming_down = true;
								level--;
							}
						} while( level || coming_down || ++current_file_i < files_len );

						/* Write out remaining chunks */
						for( i = 0; i < dir_chunks_len; i++ ){
							deinit_level( i );
							prev_chunk++;
							if( !i ) last_listing_chunk = prev_chunk;
							/* May as well free up some core */
							FREE( dir_chunks[i].chunk );
						}

						FREE( dir_chunks );
						if( feof( f ) || ferror( f ) ){
							printf( "ERROR: Output file write error\n" );
							exit( -1 );
						}
					}

					content_offset = ftell(f);

					fwrite( nlf, nlfs, 1, f ); /* Output the ::DataSpace/NameList file */
					if( atl ) fwrite( tlb ? tlfb : tlf, tlfs, 1, f ); /* Output the ::DataSpace/Storage/MSCompressed/Transform/List file */
					fwrite_QWORD( f, lzxr.len_uncompressed_input ); /* Output the ::DataSpace/Storage/MSCompressed/SpanInfo file */

					{ /* Output the ::DataSpace/Storage/MSCompressed/ControlData file */
						char ct[4] = "LZXC";
						fwrite_DWORD( f, 6 );
						fwrite( ct, 4, 1, f );
						fwrite_DWORD( f, 2 );
						fwrite_DWORD( f, hri );
						fwrite_DWORD( f, wsc );
						fwrite_DWORD( f, 0 ); /* FIXME: Work out what this means (sometimes 2, sometimes 1, sometimes 0) */
						fwrite_DWORD( f, 0 );
					}

					{ /* Output the ::DataSpace/Storage/MSCompressed/Transform/{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/ResetTable file */
						fwrite_DWORD( f, rtv );
						fwrite_DWORD( f, reset_table_len+1 );
						fwrite_DWORD( f, 8 );
						fwrite_DWORD( f, 0x28 );
						fwrite_QWORD( f, lzxr.len_uncompressed_input );
						fwrite_QWORD( f, lzxr.len_compressed_output );
						fwrite_QWORD( f, LZX_FRAME_SIZE );
						fwrite_QWORD( f, 0 ); /* First entry */
						for( i = 0; i < reset_table_len; i++ ) /* Rest of the entries */
							fwrite_QWORD( f, reset_table[i] );
					}

#if 0
					/* FIXME: Implement this bit */
					/* Output the author & internal content section 0 files */
					for(i = 0; i < cs0_files_len; i++){
						FILE* src = fopen( files[cs0_files[i]]->name, "rb" );
						if( src ){
							int n = fcat( f, src );
							if( !n && ferror(src) )
								printf( "WARNING: Read error on %s%s (ignoring)\n", prefix+1, files[cs0_files[i]]->name );
							if( !n && ferror(f) ){
								printf("ERROR: Output file write error\n");
								exit(-1); /* Uhhg we were so close */
							}
							fclose( src );
						} else printf( "WARNING: Failed opening %s%s (ignoring)\n", prefix+1, files[cs0_files[i]]->name );
					}
#endif

					{ /* Output the ::DataSpace/Storage/MSCompressed/Content file */
						int n;
						fseek( tf, 0, SEEK_SET );
						n = fcat( f, tf );
						if( ferror(tf) || ferror(f) ){
							printf( "ERROR: Output file write error or temporary file read error\n" );
							exit( -1 ); /* Dammit */
						}
					}

					{ /* Now fix up the bits missed previously */
						QWORD flen;
						fseek( f, 0, SEEK_END );
						flen = ftell( f );
						fseek( f, 0x50, SEEK_SET );
						fwrite_QWORD( f, 0x54+dcs*(prev_chunk+1) ); /* Header section 1 length */
						if( ihv == 3 ){
							fwrite_QWORD( f, content_offset ); /* Content section offset */
						}
						fseek( f, 8, SEEK_CUR );
						fwrite_QWORD( f, flen ); /* File size */
						fseek( f, 32, SEEK_CUR );
						fwrite_DWORD( f, dir_chunks_len ); /* Directory depth */
						fwrite_DWORD( f, prev_chunk ? prev_chunk : 0xffffffff ); /* Root index chunk */
						fseek( f, 4, SEEK_CUR );
						fwrite_DWORD( f, last_listing_chunk ); /* Last listing chunk */
						fseek( f, 4, SEEK_CUR );
						fwrite_DWORD( f, ++prev_chunk ); /* Number of directory chunks */
					}

					/* Are we done or what !! */
					if( ferror( f ) ){
						printf( "ERROR: Output file write error\n" );
						exit( -1 ); /* Crap */
					}

					/* We are done, now don't delete all our work */
					rm_output = false;

				} else {
					printf( "ERROR: LZX initialization failed\n" );
					exit( -1 );
				}
				fclose( tf );
			} else {
				printf( "ERROR: Could not open temporary output file\n" );
				exit( -1 );
			}
			fclose( f );
		} else {
			printf( "ERROR: Could not open output file %s\n", output_file );
			err = -1;
		}
	} else {
		printf( "ERROR: Filename malloc failed\n" );
		err = -1;
	}

	return err;
}



/* In core directory item management */

/* Recursively add a directory */
static int add_dir_contents( char*d ){
	DIR* dir = opendir( d );
	if( dir ){
		int n = 0;
		struct stat st;
		struct dirent* de;
		while( de = readdir( dir ) ){
			chdir( d );
			if( !strcmp( de->d_name, "." ) || !strcmp( de->d_name, ".." ) )
				goto END_DIR;
			if( stat( de->d_name, &st ) != 0 ){
				printf( "WARNING: Could not stat %s%s (ignoring)\n", prefix+1, de->d_name );
				goto END_DIR;
			}
			if( S_ISDIR( st.st_mode ) ){
				if( st.st_nlink > 1 ){
					/* FIXME: figure out a way for this to happen */
					printf( "WARNING: Hard linked directories in the ITSF format? Nah thats pushing it. Maybe in a future version. Dirs hard linked to %s%s will be duplicated instead.\n", prefix+1, de->d_name );
				}
				{
					size_t old_prefix_len = prefix_len;
					if( !flat ){
						/* Update the prefix */
						/* In C++: prefix += de->d_name + "/"; */
						size_t new_prefix_size, new_prefix_len = prefix_len + strlen( de->d_name ) + 1;
						if( new_prefix_len > 0xff ){
							/* FIXME: Truncate the name */
							printf( "WARNING: The ITSF format does not support dirs with internal names longer than 255 characters (ignoring %s%s/)\n", prefix+1, de->d_name );
							goto END_DIR;
						}
						new_prefix_size = ((new_prefix_len+1)/prefix_grow+1)*prefix_grow;
						if( new_prefix_size > prefix_size ){
							char* new_prefix = (char*)realloc( prefix, new_prefix_size );
							if( !new_prefix ){
								printf( "WARNING: Could not realloc prefix (ignoring %s%s/)\n", prefix+1, de->d_name );
								goto END_DIR;
							}
							prefix = new_prefix;
							prefix_size = new_prefix_size;
						}
						strcpy( prefix+prefix_len, de->d_name );
						prefix[new_prefix_len-1] = '/';
						prefix[new_prefix_len] = '\0';
						prefix_len = new_prefix_len;
						/* Add the item */
						if( ad && add_dir() == -1 ){
							printf( "WARNING: Could not add the directory %s (still adding its contents)\n", prefix+1 );
							n--; /* To compensate for not going to END_DIR */
						}
					}
					/* Get the dir contents */
					/* WARNING: Recursion */
					if( add_dir_contents( de->d_name ) == -1 ){
						printf( "WARNING: Could not open directory %s (ignoring)\n", prefix+1 );
						if( !flat ){
							prefix_len = old_prefix_len;
							prefix[prefix_len] = '\0';
						}
						goto END_DIR;
					}
					if( !flat ){
						prefix_len = old_prefix_len;
						prefix[prefix_len] = '\0';
					}
				}
			} else {
				/* FIXME: prevent compiling of non-regular files? */
				if( st.st_nlink > 1 ){
					/* FIXME: figure out a way for this to happen */
					printf( "WARNING: The ITSF format does not specifically support hard linked files, but they can be faked, kind of, and it depends on the decompressor. Not in this version though. Files hard linked to %s%s will be duplicated instead.\n", prefix+1, de->d_name );
				}
				if( strlen( de->d_name ) + prefix_len > 0xff ){
					/* FIXME: Truncate the name */
					printf( "WARNING: The ITSF format does not support files with internal names longer than 255 characters (ignoring %s%s/)\n", prefix+1, de->d_name );
					goto END_DIR;
				}
				if( add_file( de->d_name ) == -1 ){
					printf( "WARNING: Could not add file %s%s (ignoring)\n", prefix+1, de->d_name );
					goto END_DIR;
				}
			}
			n++;
END_DIR: /* Sorry about the gotos */
			chdir( ".." );
		}
		closedir( dir );
		return n;
	}
	return -1;
}

/* Add dir to the directory */
static int add_dir( void ){
	/* Ensure enough room */
	size_t new_files_size = (((files_len+1)/files_grow)+1)*files_grow;
	if( new_files_size > files_size ){
		item_t** new_files = (item_t**)realloc( files, sizeof(item_t*)*new_files_size );
		if( !new_files ){
			return -1;
		}
		files = new_files;
		files_size = new_files_size;
	}

	{
		item_t* new_item = (item_t*)malloc( sizeof(item_t) );
		if(new_item){
			char* new_name = (char*)malloc( prefix_len + 1 );
			if( new_name ){
				strcpy( new_name, prefix );
				new_item->name_len = prefix_len;
				new_item->name = new_name;
				new_item->cs =
				new_item->offset =
				new_item->length = 0;
				new_item->flags = 0;
				files[files_len++] = new_item;
				return 0;
			} else {
				free( new_item );
			}
		}
	}
	return -1;
}

/* Add file to the directory */
static int add_file( char* file ){
	/* Ensure enough room */
	size_t new_files_size = (((files_len+1)/files_grow)+1)*files_grow;
	if( new_files_size > files_size ){
		item_t** new_files = (item_t**)realloc( files, sizeof(item_t*)*new_files_size );
		if( !new_files ){
			return -1;
		}
		files = new_files;
		files_size = new_files_size;
	}

	{
		item_t* new_item = (item_t*)malloc( sizeof(item_t) );
		if(new_item){
			size_t new_name_len = prefix_len + strlen(file);
			char* new_name = (char*)malloc( new_name_len + 1 );
			if(new_name){
				strcpy( new_name, prefix );
				strcpy( new_name + prefix_len, file );
				new_item->name_len = new_name_len;
				new_item->name = new_name;
				new_item->cs =  def_cs;
				new_item->flags = 0;
				/* Other fields will be updated later */
				files[files_len++] = new_item;
				return 0;
			} else {
				free( new_item );
			}
		}
	}
	return -1;
}

/* Add a ready-made struct to the directory */
static int add_item( item_t* new_item ){
	size_t new_files_size = (((files_len+1)/files_grow)+1)*files_grow;
	if( new_files_size > files_size ){
		item_t** new_files = (item_t**)realloc( files, sizeof(item_t*)*new_files_size );
		if( !new_files ){
			return -1;
		}
		files = new_files;
		files_size = new_files_size;
	}

	files[files_len++] = new_item;

	return 0;
}

static void sort_items( void ){
	qsort(files,files_len,sizeof(item_t*),compare_items);
}

static int compare_items( const void* arg1, const void* arg2 ){
	return strcasecmp((*(item_t**)arg1)->name,(*(item_t**)arg2)->name);
}



/* LZX callbacks follow */

/*
get_bytes
This function is designed to get as much data as possible
It will open every file if need be
Multiple calls to this file also work properly
It also initialises the file structures
*/
static int get_bytes( void* arg, int n, void* buf ){
	int read = 0, just_read, to_read = n;
	for(; current_file_i < files_len ; current_file_i++){
		if( !files[current_file_i]->cs ) continue;
		if( !current_file ){
			current_file = fopen(files[current_file_i]->name+1, "rb");
			if( !current_file ){
				files[current_file_i]->cs =
				files[current_file_i]->offset =
				files[current_file_i]->length = 0;
				printf( "ERROR: Failed opening %s\n", files[current_file_i]->name+1 );
				continue;
			}
			printf( "Reading %s ... ", files[current_file_i]->name+1 );
			current_len = 0;
			files[current_file_i]->offset = current_offset;
		}
		just_read = fread( buf+read, 1, to_read, current_file );
		to_read -= just_read;
		read += just_read;
		current_len += just_read;
		current_offset += just_read;
		if( read < n ){
			printf( "done\n" );
			fclose(current_file); current_file = NULL;
			files[current_file_i]->length = current_len;
			if(!current_len){
				files[current_file_i]->cs =
				files[current_file_i]->offset = 0;
			}
		} else return read;
	}
	return read;
}

static int put_bytes( void* arg, int n, void* buf ){
	return fwrite(buf, 1, n, (FILE*)arg);
}

static void mark_frame( void* arg, DWORD uncomp, DWORD comp ){
	size_t new_reset_table_size = (((reset_table_len+1)/reset_table_grow)+1)*reset_table_grow;
	num_frames++;
	/* Resize reset table if need be */
	if( new_reset_table_size > reset_table_size ){
		QWORD* new_reset_table = (QWORD*)realloc(reset_table,sizeof(QWORD)*new_reset_table_size);
		if( !new_reset_table ){
			/* Oh shit! not enough memory, no way to tell the decompressor and we can't exit cause core will be leaked by lzx! what now? */
			printf( "ERROR: Failed to increase the size of the ResetTable (a buggy reset table will be output)\n");
			return /*-1*/; /* When lzx allows callbcks to return negative on error this will work */
		}
		reset_table = new_reset_table;
		reset_table_size = new_reset_table_size;
	}

	/* Store info in reset table */
	reset_table[reset_table_len++] = comp;
	return /*0*/;
}

static int at_eof( void* arg ){
	return !files_len || (current_file_i >= files_len && (!current_file || feof(current_file)));
}



/* Binary output prototypes */

static int fwrite_WORD( FILE* f, const WORD i ){
	BYTE out[2] = {i,i>>8};
	return fwrite(out,2,1,f);
}

static int fwrite_DWORD( FILE* f, const DWORD i ){
	BYTE out[4] = {i,i>>8,i>>16,i>>24};
	return fwrite(out,4,1,f);
}

static int fwrite_QWORD( FILE* f, const QWORD i ){
	BYTE out[8] = {i,i>>8,i>>16,i>>24,i>>32,i>>40,i>>48,i>>56};
	return fwrite(out,8,1,f);
}

static int fwrite_GUID( FILE* f, const GUID* i ){
	BYTE out[16] = {
		i->Data1,i->Data1>>8,i->Data1>>16,i->Data1>>24,
		i->Data2,i->Data2>>8,
		i->Data3,i->Data3>>8,
		i->Data4[0],i->Data4[1],i->Data4[2],i->Data4[3],
		i->Data4[4],i->Data4[5],i->Data4[6],i->Data4[7]
	};
	return fwrite(out,16,1,f);
}



/* Directory output helper functions */

static void malloc_level( uint level ){

	/* Grow the array */
	size_t new_dir_chunks_len = level + 1;
	size_t new_dir_chunks_size = ((new_dir_chunks_len/dir_chunks_grow)+1)*dir_chunks_grow;
	if( new_dir_chunks_size > dir_chunks_size ){
		chunk_t* new_dir_chunks = (chunk_t*)realloc(dir_chunks,sizeof(chunk_t)*new_dir_chunks_size);
		if( !new_dir_chunks ){
			printf( "ERROR: Could not get enough memory for a level %u directory chunk item\n", level );
			exit( -1 );
		}
		dir_chunks = new_dir_chunks;
		dir_chunks_size = new_dir_chunks_size;
	}

	/* Get a chunk */
	dir_chunks[level].chunk = (BYTE*) malloc( dcs - 2 - ( level ? 8 : 20 ) );
	if( !dir_chunks[level].chunk ){
		printf( "ERROR: Could not get enough memory for a level %u directory chunk\n", level );
		exit( -1 );
	}

	/* We got more */
	dir_chunks_len = level + 1;
}

static void init_level( uint level ){
	if( level ) init_index_chunk( level );
	else init_listing_chunk( );
}

static void deinit_level( uint level ){
	if( zfs ) memset( dir_chunks[level].current_offset, 0, dir_chunks[level].space_left );
	fwrite( level ? "PMGI" : "PMGL", 4, 1, output_file_h );
	fwrite_DWORD( output_file_h, dcs - ( level ? 8 : 20 ) - (dir_chunks[level].current_offset - dir_chunks[level].chunk) );
	if( !level ){
		fwrite_DWORD( output_file_h, 0 );
		fwrite_DWORD( output_file_h, prev_chunk );
		fwrite_DWORD( output_file_h, 0xffffffff ); /* We'll come back to this later */
	}
	fwrite( dir_chunks[level].chunk, dcs - 2 - ( level ? 8 : 20 ), 1, output_file_h );
	fwrite_WORD( output_file_h, dir_chunks[level].num_entries );
}

static void init_index_chunk( uint level ){
	chunk_t* index_chunk = &dir_chunks[level];
	/* Init vars */
	index_chunk->space_left = dcs - 8 - 2;
	index_chunk->current_offset = index_chunk->chunk;
	index_chunk->num_entries = 0;
	index_chunk->current_quickref = index_chunk->chunk + dcs - 8 - 2 - 1;
	index_chunk->current_index = 0;
}

static void init_listing_chunk( void ){
	/* Init vars */
	dir_chunks->space_left = dcs - 20 - 2;
	dir_chunks->current_offset = dir_chunks->chunk;
	dir_chunks->num_entries = 0;
	dir_chunks->current_quickref = dir_chunks->chunk + dcs - 20 - 2 - 1;
	dir_chunks->current_index = 0;
}


static uint add_item_to_chunk( uint level, item_t* item, ENCINT listing_chunk ){
	chunk_t* chunk;
	uint req_space;

	/* There can only be a maximum of 0xffff entries per chunk */
	if( dir_chunks[level].num_entries == 0xffff )
		return 1;

	chunk = &dir_chunks[level];
	req_space = 1 + item->name_len;

	if( level ) {
		req_space += enc_int_len( listing_chunk );
	} else {
		req_space += enc_int_len( item->cs );
		req_space += enc_int_len( item->offset );
		req_space += enc_int_len( item->length );
	}

	if( chunk->current_index && !(chunk->current_index%tqd) )
		req_space += 2;

	if( req_space > chunk->space_left ){
		/* Not enough space */
		if( !chunk->current_index ){
			/* We didn't put any in it */
			printf("ERROR: directory chunk size is too small\n");
			exit( -1 );
		}
		/* Filled */
		return 1;
	}

	/* Write quickref entry */
	if( chunk->current_index && !(chunk->current_index%tqd) ){
		*(chunk->current_quickref--) = (chunk->current_offset - chunk->chunk) >> 8;
		*(chunk->current_quickref--) = (chunk->current_offset - chunk->chunk);
	}

	/* Write directory index entry */
	*(chunk->current_offset++) = item->name_len;
	memcpy( chunk->current_offset, item->name, item->name_len );
	chunk->current_offset += item->name_len;
	if( level ) {
		chunk->current_offset += write_enc_int( chunk->current_offset, listing_chunk );
	} else {
		chunk->current_offset += write_enc_int( chunk->current_offset, item->cs );
		chunk->current_offset += write_enc_int( chunk->current_offset, item->offset );
		chunk->current_offset += write_enc_int( chunk->current_offset, item->length );
	}

	/* Increase the number of entries in the chunk */
	chunk->num_entries++;

	/* Decrease the space left */
	chunk->space_left -= req_space;

	chunk->current_index++;

	return 0;  /* Not filled */
}



/* ENCINT helper functions */

static uint enc_int_len( ENCINT ei ){
	uint bit = sizeof(ENCINT)*8/7*7, ret;
	ENCINT mask;
	for(;;){
		mask = 0x7f << bit;
		if( !bit || (ei & mask) ) break;
		bit -=7;
	}
	ret = 0;
	for(;;){
		if(bit == 0) break;
		bit -=7;
		ret++;
	}
	return ++ret;
}

static uint write_enc_int( BYTE* buf, ENCINT ei ){
	uint bit = sizeof(ENCINT)*8/7*7, ret;
	ENCINT mask;
	for(;;){
		mask = 0x7f << bit;
		if( !bit || (ei & mask) ) break;
		bit -=7;
	}
	ret = 0;
	for(;;){
		*buf = (BYTE)((ei>>bit)&0x7f);
		if(bit == 0) break;
		*(buf++) |= 0x80;
		bit -=7;
		ret++;
	}
	return ++ret;
}



/* Data prototypes */

static BYTE fcat_buffer[FCAT_BUFFER_SIZE];

static int fcat( FILE* sink, FILE* source ){
	int n, ret = 0;
	do{
		n = fread( fcat_buffer, 1, FCAT_BUFFER_SIZE, source );
		n = fwrite( fcat_buffer, 1, n, sink );
		ret += n;
	} while( n == FCAT_BUFFER_SIZE );
	return ret;
}



/* Misc. functions */

static void usage( void ){
	printf(
		"Usage: "PROGNAME" [options] <dir> | <project.hhp>\n"
		"Converts <dir> into an .its file\n"
		"OR\n"
		"Converts <project.hhp> into a .chm file (currently not implemented)\n"
		"\n"
		PROGNAME" "VERSION" (C) 2002 Pabs <pabs3@zip.to>\n"
		"This is free software with ABSOLUTELY NO WARRANTY.\n"
	);
}

static void on_exit( void ){
	if( rm_output ){ /* This is turned off on successful completion */
		fclose( output_file_h );
		remove( output_file );
	}
	free_all( );
}

static void free_all( void ){
	uint i;
	FREE( prefix );
	if( files ){
		for( i = 0; i < files_len; i++ ){
			if( !(files[i]->flags & F_DONT_FREE) ){
				FREE( files[i]->name );
				FREE( files[i] );
			}
		}
		FREE( files );
	}
	FREE( reset_table );
	FREE( output_file );
#if 0
	FREE( cs0_files );
	FREE( cs1_files );
#endif
	if( dir_chunks ){
		for( i = 0; i < dir_chunks_len; i++ ){
			FREE( dir_chunks[i].chunk );
		}
		FREE( dir_chunks );
	}
}
