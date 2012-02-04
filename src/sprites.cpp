/*****************************************\
*                                         *
* SPRITES.C - A couple of routines to     *
*             decode and encode TTD .GRF  *
*             sprites                     *
*                                         *
*                                         *
* Copyright (C) 2000 by Josef Drexler     *
*               <jdrexler@julian.uwo.ca>  *
*                                         *
* Permission granted to copy and redist-  *
* ribute under the terms of the GNU GPL.  *
* For more info please read the file      *
* COPYING which should have come with     *
* this file.                              *
*                                         *
\*****************************************/

#include <stdlib.h>
#include "error.h"

#include "sprites.h"
#include "grfcomm.h"

int maxx = 0, maxy = 0, maxs = 0;

static int decodetile(U8 *buffer, int sx, int sy, U8 *imgbuffer, int grfcontversion)
{
	U16 *ibuffer = (U16*) buffer;

	for (int y=0; y<sy; y++) {
		long offset = BE_SWAP16(ibuffer[SpriteInfo::Size(grfcontversion) / 2 + y]) + SpriteInfo::Size(grfcontversion);

		long x, islast, chunkstart=0;
		do {
			islast   = buffer[offset]   & 0x80;
			long len = buffer[offset++] & 0x7f;
			long ofs = buffer[offset++];

			// fill from beginning of last chunk to start
			// of this one, with "background" colour
			for (x=chunkstart; x<ofs; x++)
				*imgbuffer++ = 0;

			// then copy the number of actual bytes
			for (x=0; x<len; x++) {
				int col = buffer[offset++];
				*imgbuffer++ = col;
			}
			chunkstart = ofs + len;

		} while (!islast);

		// and fill from the end of the last chunk to the
		// end of the line
		for (x=chunkstart; x<sx; x++)
			*imgbuffer++ = 0;
	}

	return 1;
}

static int decoderegular(const U8 *buffer, int sx, int sy, U8 *imgbuffer, int grfcontversion)
{
	long offset = SpriteInfo::Size(grfcontversion);
	for (int y=0; y<sy; y++) {
		for (int x=0; x<sx; x++)
			*imgbuffer++ = buffer[offset++];
	}

	return 1;
}

static long uncompress(unsigned long size, U8* in, unsigned long *insize, U8* out, unsigned long outsize, int spriteno, int grfcontversion)
{
	unsigned long inused, datasize, compsize, *testsize;

	const int infobytes = SpriteInfo::Size(grfcontversion);
	memcpy(out, in, infobytes);

	testsize = &datasize;
	if (grfcontversion == 2 || SIZEISCOMPRESSED(in[0]))	// size is the compressed size
		testsize = &compsize;

	compsize = infobytes;		// initially we only have the info data
	datasize = infobytes;
	in += infobytes;
	inused = infobytes;

	while (*testsize < size) {
		S8 code = * ( (S8*) in++);
		if (++inused > *insize)
			break;

		compsize++;

		if (code < 0) {
			U8 ofs = *(in++);
			inused++;

			compsize++;

			unsigned long count = -(code >> 3);
			unsigned long offset = ( (code & 7) << 8) | ofs;

			if (datasize < offset) {
				printf("\nOffset too large in sprite %d!\n", spriteno);
				exit(2);
			}

			if (datasize + count > outsize)
				return -2*(datasize + count);

			U8* copy_src = out + datasize - offset;
			U8* copy_dest = out + datasize;
			for(unsigned long i = 0; i < count; i++) {
				*copy_dest = *copy_src; copy_dest++; copy_src++;
			}

			datasize += count;
		} else {
			unsigned long skip = code;
			if (code == 0)
				skip = 128;

			if (datasize + skip > outsize)
				return -2*(datasize + skip);

			memmove(out+datasize, in, skip);
			in += skip;
			inused += skip;

			compsize += skip;
			datasize += skip;
		}
	}

	if (inused > *insize) {
		printf("\nNot enough input data for decompression of sprite %d\n", spriteno);
		exit(2);
	}

	*insize = inused;
	return datasize;
}

void SpriteInfo::writetobuffer(U8 *buffer, int grfcontversion)
{
	int i = 0;
	if (grfcontversion == 2) {
		/* Copy bits 3 and 6 of the nfo and make it, for now, an 8bpp normal GRF. */
		buffer[i++] = 0x04 | (this->info & (1 << 3 | 1 << 6));
		buffer[i++] = this->zoom;
		buffer[i++] = this->ydim & 0xFF;
		buffer[i++] = this->ydim >> 8;
	} else {
		buffer[i++] = this->info;
		buffer[i++] = this->ydim;
	}
	buffer[i++] = this->xdim & 0xFF;
	buffer[i++] = this->xdim >> 8;
	buffer[i++] = this->xrel & 0xFF;
	buffer[i++] = this->xrel >> 8;
	buffer[i++] = this->yrel & 0xFF;
	buffer[i++] = this->yrel >> 8;
}

void SpriteInfo::readfromfile(const char *action, int grfcontversion, FILE *grf)
{
	int i = 0;
	if (grfcontversion == 2) {
		/* According to the documentation bit 0 must always be set, and bits 3 and 6 are the same as in the nfo. */
		this->info = (1 << 0) | (fgetc(grf) & (1 << 3 | 1 << 6));
		this->zoom = fgetc(grf);
		this->ydim = readword(action, grf);
		i += 2;
	} else {
		this->info = fgetc(grf);
		this->zoom = 0;
		this->ydim = fgetc(grf);
	}
	this->xdim = readword(action, grf);
	this->xrel = readword(action, grf);
	this->yrel = readword(action, grf);
}

int decodesprite(FILE *grf, spritestorage *store, spriteinfowriter *writer, int spriteno, U32 *dataoffset, int grfcontversion)
{
	static const char *action = "decoding sprite";
	unsigned long size, datasize, inbufsize, outbufsize, startpos, returnpos = 0;
	SpriteInfo info;
	U8 *inbuffer, *outbuffer, *imgbuffer;
	int sx, sy;

	if (!writer || !store) {
		printf("\nparameter is NULL!\n");
		exit(2);
	}

	store->newsprite();

	size = readspritesize(action, grfcontversion, grf);
	if (size == 0) return 0;

	startpos = ftell(grf);
	U8 inf;
	cfread(action, &inf, 1, 1, grf);
	U32 id = 0;

	if (grfcontversion == 2 && inf == 0xfd) {
		// Jump to the data offset
		id = readdword(action, grf);
		returnpos = ftell(grf);

		fseek(grf, *dataoffset, SEEK_SET);
	}

	long result;
	for (int i = 0;; i++) {
		if (returnpos != 0) {
			U32 dataid = readdword(action, grf);
			if (id != dataid) {
				if (i > 0) break;
				printf("\nUnexpected id for sprite %d; expected %d got %d\n", spriteno, id, dataid);
				exit(2);
			}
			size = readdword(action, grf);
			if (size == 0) {
				printf("\nUnexpected size for sprite %d; expected non-zero got %d\n", spriteno, (int)size);
				exit(2);
			}

			startpos = ftell(grf);
			cfread(action, &inf, 1, 1, grf);
		}

		if (inf == 0xff) {
			store->setsize(1, 0);
			outbuffer = (U8*) malloc(size);
			//outbuffer[0] = 0xff;
			cfread(action, outbuffer, 1, size, grf);
			writer->adddata(size, outbuffer/*+1*/);
			store->spritedone();
			return 1;
		}

		fseek(grf, startpos, SEEK_SET);
		info.readfromfile(action, grfcontversion, grf);

		if(info.zoom >= ZOOM_LEVELS){
			printf("\nUnknown zoom level %02x for sprite %d\n", info.zoom, spriteno);
			exit(2);
		}

		sx = info.xdim;
		sy = info.ydim;
		const int infobytes = SpriteInfo::Size(grfcontversion);
		outbufsize = 0L + sx * sy + infobytes;

		if (grfcontversion == 2 && HASTRANSPARENCY(info.info)) size -= 4;

		if (grfcontversion == 2 || SIZEISCOMPRESSED(info.info)) {	// compressed size stated
			inbufsize = size;
			// assume uncompressed is max. twice the compressed
			outbufsize <<= 1;
		} else {
			// assume compressed is max 12% larger than uncompressed (overhead etc.)
			inbufsize = size + (size >> 3);
		}

		if (HASTRANSPARENCY(info.info)) {
			outbufsize += 0L + info.ydim * 4;
		}

		inbuffer = (U8*) malloc(inbufsize);
		outbuffer = (U8*) malloc(outbufsize);
		imgbuffer = (U8*) calloc(sx * sy, 1);
		if (!inbuffer || !outbuffer || !imgbuffer) {
			printf("\nError allocating sprite buffer, want %ld for sprite %d\n", inbufsize + outbufsize + sx * sy, spriteno);
			exit(2);
		}

		do {
			fseek(grf, startpos, SEEK_SET);
			if (grfcontversion == 2 && HASTRANSPARENCY(info.info)) {
				int tmp = fread(inbuffer, 1, infobytes, grf);
				readdword(action, grf);
				inbufsize = tmp + fread(inbuffer + infobytes, 1, inbufsize - infobytes, grf);
			} else {
				inbufsize = fread(inbuffer, 1, inbufsize, grf);
			}
			if (inbufsize == 0) {
				printf("\nError reading sprite %d\n", spriteno);
				exit(2);
			}
			result = uncompress(size, inbuffer, &inbufsize, outbuffer, outbufsize, spriteno, grfcontversion);
			if (result < 0) {
				outbufsize = -result;
				outbuffer = (U8*) realloc(outbuffer, outbufsize);
				if (!outbuffer) {
					printf("\nError increasing sprite buffer size for sprite %d\n", spriteno);
					exit(2);
				}
			}
		} while (result < 0);
		datasize = result;

		if (HASTRANSPARENCY(info.info))	// it's a tile
			result = decodetile(outbuffer, sx, sy, imgbuffer, grfcontversion);
		else
			result = decoderegular(outbuffer, sx, sy, imgbuffer, grfcontversion);

		store->setsize(sx, sy);
		writer->addsprite(i == 0, store->curspritex(), info);
		for (int y=0; y<sy; y++) {
			for (int x=0; x<sx; x++)
				store->nextpixel(CommonPixel(0, 0, 0, 0, imgbuffer[y * sx + x]));
			store->newrow();
		}
		store->spritedone(sx, sy);


		if (sy > maxy)
			maxy = sy;
		if (sx > maxx)
			maxx = sx;
		if (datasize > (unsigned long) maxs)
			maxs = datasize;

		free(inbuffer);
		free(outbuffer);
		free(imgbuffer);

		if (returnpos != 0) {
			*dataoffset = startpos + inbufsize;
		} else {
			returnpos = startpos + inbufsize;
			break;
		}
	}
	fseek(grf, returnpos, SEEK_SET);
	return result;
}

static long fakecompress(const U8 *in, long insize, U8 *out, long outsize, U16 *compsize)
{
	long needsize = insize + ((insize + 0x7f) / 0x7f);
	if (outsize < needsize) {
		needsize += needsize / 8;
		return -needsize;
	}

	long inpos = 0, outpos = 0;
	while (inpos < insize) {
		long rest = insize - inpos;
		if (rest > 0x7f)
			rest = 0x7f;

		out[outpos++] = rest;
		memmove(&(out[outpos]), &(in[inpos]), rest);
		outpos += rest;
		inpos += rest;
	}
	if (outpos > 65535) {
		printf("\nSorry, uncompressed sprite too large\n");
		exit(2);
	}
	*compsize = outpos;

	return 1;
}

#ifdef OLDSTRATEGY
// different compression strategies for identifying repetition
static multitype strategy1(const U8* data, unsigned int datasize, unsigned int datamax)
{
	int overlap;
	multitype ret;
	unsigned int ofs, foundofs = GREP_NOMATCH;

	// can only find up to 11 bits back
	if (datasize >= (1 << 11)) {
		ofs = datasize - (1 << 11) + 1;
		data+=ofs;
		datasize-=ofs;
	}
	for (overlap=MAXOVERLAP;overlap >= MINOVERLAP;overlap--) {
		// see if we can find a repetition of length overlap

		// allow overlapping memory blocks?
#if 0
		foundofs = grepmem(data, datasize+overlap-1, data+datasize, overlap, 0);
#else
		foundofs = grepmem(data, datasize, data+datasize, overlap, 0);
#endif

		if (foundofs != GREP_NOMATCH) {		// found one
			if (foundofs > datasize) {
				foundofs = GREP_NOMATCH;	// not really
				continue;
			}
			ofs = datasize - foundofs;
			if (ofs + overlap > datasize)
				overlap = datasize - ofs;
			if (datasize + overlap > datamax)
				overlap = datamax - datasize;
			break;
		}
	}

	ret.u16[0] = ofs;
	ret.u8[2] = overlap;
	ret.u8[3] = (foundofs != GREP_NOMATCH) && (overlap >= MINOVERLAP);
	return ret;
}
#endif

// use strategy from LZ77 algorithm
//
// data is the beginning of the sprite data
// datasize is how much has been processed so far, and can be used for
//	the repetition finding
// datamax is the entire size of the sprite data
static multitype strategy2(const U8* data, unsigned int datasize, unsigned int datamax)
{
	unsigned int overlap = 0, newoverlap, remain, minoverlap, maxoverlap;
	multitype ret;
	int foundofs = -1;
	const U8 *found;

	// can only find up to 11 bits back
	if (datasize >= (1 << 11)) {
		unsigned int ofs = datasize - (1 << 11) + 1;
		data+=ofs;
		datasize-=ofs;
		datamax-=ofs;
	}

	remain = datasize;
	found = data-1;
	maxoverlap = MAXOVERLAP;

	while (overlap < maxoverlap) {

		// minoverlap is minimum overlap we need to find minus one
		minoverlap = overlap;
		if (minoverlap < MINOVERLAP-1)
			minoverlap = MINOVERLAP-1;

		if (remain <= minoverlap)
			break;		// don't have more data to find longer overlap

		int i;
		for (i=remain-minoverlap; i; i--)
			if ( *(++found) == data[datasize])
				break;

		if (!i)
			break;

		foundofs = (int) (found - data);
		remain = datasize - foundofs - 1;

		maxoverlap = datasize - foundofs;
		if (maxoverlap > MAXOVERLAP)
			maxoverlap = MAXOVERLAP;		// can't store more anyway
		if (maxoverlap > datamax - datasize)
			maxoverlap = datamax - datasize;	// can't use more

		newoverlap = 1;
		while ( (newoverlap < maxoverlap) && (found[newoverlap] == data[datasize+newoverlap]) )
			newoverlap++;

		if (newoverlap < MINOVERLAP)
			continue;

		if (newoverlap > overlap) {	// a longer chunk
			ret.u16[0] = datasize - foundofs;
			overlap = newoverlap;
		}
	}
	ret.u8[2] = overlap;
	ret.u8[3] = (overlap >= MINOVERLAP);
	return ret;
}


#ifdef _MSC_VER
#pragma warning(disable:4701)//chunk may be used uninitialized
#endif

static long realcompress(const U8 *in, long insize, U8 *out, long outsize, U16 *compsize)
{
	long inpos = 0, outpos = 0;
	U8 *lastcodepos = out;
	multitype chunk;

	out[outpos++] = 1;
	out[outpos++] = in[inpos++];

	while (inpos < insize) {
		// search for where the first repetition of >= 3 chars occurs
		int overlap;
		int ofsh,ofsl;

		//	chunk = strategy1(in, inpos, insize);
		chunk = strategy2(in, inpos, insize);

		if (chunk.u8[3]) {		//  Yay! Found one!
			if (!*lastcodepos)	// there's a zero length verbatim chunk -> delete it
				outpos--;

			ofsl = chunk.u8[BYTE_OFSL];
			ofsh = chunk.u8[BYTE_OFSH];
			overlap = chunk.u8[2];

			out[outpos++] = (-overlap << 3) | ofsh;
			out[outpos++] = ofsl;

			out[outpos] = 0;	// start new interim verbatim chunk
			lastcodepos = &(out[outpos++]);
			inpos += overlap;
		} else {	//  no we didn't. Increase length of verbatim chunk
			if (*lastcodepos == 0x7f) {	// maximum length 127
				lastcodepos = &(out[outpos++]);	// start new one
				*lastcodepos = 0;
			}
			(*lastcodepos)++;
			out[outpos++] = in[inpos++];
		}
		if (outpos + 2 >= outsize) {
			// buffer too small, estimate expected size
			long needed = (long) outpos * (long) insize / (long) inpos;
			needed += needed >> 3;	// add 12% extra
			return -needed;
		}
	}

	if (inpos != insize) {
		printf("\nError: compressed %ld bytes too much: %ld not %ld!",
			inpos - insize, inpos, insize);
		printf("\nLast chunk was repetition=%d, len=%d, ofs=%d\n",
			chunk.u8[3], chunk.u8[2], chunk.u16[0]);
		exit(2);
	}

	if (!*lastcodepos)
		outpos--;

	*compsize = outpos;

	return 1;
}

#ifdef _MSC_VER
#pragma warning(default:4701)
#endif

static U16 lasttilesize;

U16 getlasttilesize()
{
	return lasttilesize;
}

U16 encodetile(FILE *grf, const U8 *image, long imgsize, U8 background, int sx, int sy, SpriteInfo inf, int docompress, int spriteno, int grfcontversion)
{
	long tilesize = imgsize + 16L * sy;

	while (1) {	// repeat in case we didn't allocate enough memory
		U8 *tile = (U8*) malloc(tilesize);
		if (!tile) {
			printf("\nError: can't allocate %ld bytes for tile memory of sprite %d\n",
				tilesize, spriteno);
			exit(2);
		}

		long tileofs = 2L * sy;		// first sy (int) offsets, then data

		U16 *lineofs = (U16*) tile;
		int y;

		for (y=0; y<sy; y++) {
			int x1 = 0, x2 = 0;

			lineofs[y] = tileofs;
			lineofs[y] = BE_SWAP16(lineofs[y]);
			long lastlenofs = tileofs;

			while ( (x1 < sx) && (tileofs + 2 + sx < tilesize) ) {
				// find where next non-transparent part starts
				while ( (x1 < sx) && (image[y*sx+x1] == background) )
					x1++;

				if (x1 < sx) {
					int len = 1;
					// ...and where it ends
					x2 = x1 + 1;
					while ( (x2 < sx) && (len < 0x7f) && (image[y*sx+x2] != background) ) {
						len++;
						x2++;
					}

					if (x2 > 255) { // chunk extends past the 255-wall; encode the remainder of the line
						if (x1 > 255) // chunk cannot start after 255; move it back
							x1 = 255;
						x2 = sx;
						while ( (image[y*sx+x2-1] == background) )
							x2--;
						len = x2 - x1;
						if (len > 0x7f) { // chunk is too long
							if (x1 < 255) {  // first encode the part before the wall
								len = 255 - x1;
								x2 = 255;
							} else { // chunk starts at wall, abort
								printf("Error: Sprite %d is too wide to use tile encoding.\n", spriteno);
								exit(2);
							}
						}
					}

					lastlenofs = tileofs;
					tile[tileofs++] = len;
					tile[tileofs++] = x1;
					memmove( &(tile[tileofs]), &(image[y*sx+x1]), len);

					tileofs += len;
				} else {	// transparent to end of line
					if (x2 == 0) {	// totally empty line
						tile[tileofs++] = 0;
						tile[tileofs++] = 0;
					}
					x2 = x1;
				}
				x1 = x2;
			}
			tile[lastlenofs] |= 0x80;
			if (tileofs + 2 + sx >= tilesize)
				break;
		}

		if (tileofs + 2 + sx >= tilesize) {
			// tile didn't hold all data, estimate real size
			// and enlarge it
			free(tile);
			long imgofs = y*sx + 1L;
			tilesize = tilesize * imgsize / imgofs;
			tilesize += (tilesize >> 3) + 16L;
			continue;
		}

		lasttilesize = tileofs;

		int result = encoderegular(grf, tile, tileofs, inf, docompress, spriteno, grfcontversion);
		free(tile);
		return result;
	}
}

U16 encoderegular(FILE *grf, const U8 *image, long imgsize, SpriteInfo inf, int docompress, int spriteno, int grfcontversion)
{
	const int infobytes = SpriteInfo::Size(grfcontversion);
	long compsize = imgsize + 24 + infobytes, uncompsize = compsize + infobytes;
	unsigned int size;

	U8 *compr = (U8*) malloc(compsize);
	U8 *uncomp = (U8*) malloc(uncompsize);
	if (!compr || !uncomp) {
		printf("\nError: can't allocate %ld bytes for compressed buffer while encoding sprite %d\n",
			compsize + uncompsize, spriteno);
		exit(2);
	}

	long result;
	U16 realcompsize = 0;
	while (1) {
		inf.writetobuffer(compr, grfcontversion);
		if (docompress)
			result = realcompress(image, imgsize, compr+infobytes, compsize-infobytes, &realcompsize);
		else
			result = fakecompress(image, imgsize, compr+infobytes, compsize-infobytes, &realcompsize);

		if (grfcontversion == 2 || SIZEISCOMPRESSED(inf.info))	// write compressed size
			size = realcompsize + infobytes;
		else
			size = imgsize + infobytes;

		if (result > 0) {
			do {	// everything was good

				// check that the compression is correct, by uncompressing again
				unsigned long insize = realcompsize + infobytes;
				result = uncompress(size, compr, &insize, uncomp, uncompsize, spriteno, grfcontversion);
				if (result < 0) {
					uncompsize = -result;
					uncomp = (U8*) realloc(uncomp, uncompsize);
					if (!uncomp) {
						printf("\nError increasing sprite buffer size for sprite %d\n", spriteno);
						exit(2);
					}
				}
				// and verifying
				if ((result-imgsize-infobytes) || memcmp(uncomp+infobytes, image, imgsize)) {
					printf("\nError: invalid compression of sprite %d, ", spriteno);
					if (result-imgsize-infobytes)
						printf("length diff %ld, ", result-imgsize-infobytes);
					else {
						int i;
						for (i=0; uncomp[i+infobytes]==image[i]; i++) {}
						printf("data diff at %d of %ld bytes, ", i, imgsize);
					}
					if (docompress) {
						puts("trying without it for this sprite\n");
						docompress = 0;
					} else {
						printf("even uncompressed, aborting.\n");
						exit(2);
					}
					result = 0;
				}
			} while (result < 0);
			if (result)
				break;
		} else if (result < 0) {	// buffer was too small
			compsize = -result;
			compr = (U8*) realloc(compr, compsize);
			if (!compr) {
				printf("\nError: can't allocate %ld bytes for compressed buffer of sprite %d\n",
					compsize, spriteno);
				exit(2);
			}
		} else {
			printf("\nError: unknown error while compressing sprite %d\n", spriteno);
			exit(2);
		}
	}

	static const char *action = "writing real sprite";
	if (grfcontversion == 2) {
		writedword(action, spriteno, grf);
		if (HASTRANSPARENCY(inf.info)) size += 4;
	}
	writespritesize(action, size, grfcontversion, grf);
	cfwrite(action, compr, 1, infobytes, grf);
	if (grfcontversion == 2 && HASTRANSPARENCY(inf.info)) {
		writedword(action, imgsize, grf);
	}
	cfwrite(action, compr + infobytes, 1, realcompsize, grf);

	free(compr);
	free(uncomp);

	return realcompsize;
}

void writespritesize(const char *action, unsigned int spritesize, int grfcontversion, FILE *grf)
{
	if (grfcontversion == 2) {
		writedword(action, spritesize, grf);
	} else {
		writeword(action, spritesize, grf);
	}
}

void writeword(const char *action, unsigned int value, FILE *grf)
{
	U16 le_value = BE_SWAP16(value);
	cfwrite(action, &le_value, 1, 2, grf);
}

void writedword(const char *action, unsigned int value, FILE *grf)
{
	U32 le_value = BE_SWAP32(value);
	cfwrite(action, &le_value, 1, 4, grf);
}

unsigned int readspritesize(const char *action, int grfcontversion, FILE *grf)
{
	return grfcontversion == 2 ? readdword(action, grf) : readword(action, grf);
}

U16 readword(const char *action, FILE *grf)
{
	U16 le_value;
	cfread(action, &le_value, 1, 2, grf);
	return BE_SWAP16(le_value);
}

U32 readdword(const char *action, FILE *grf)
{
	U32 le_value;
	cfread(action, &le_value, 1, 4, grf);
	return BE_SWAP32(le_value);
}
