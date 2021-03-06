/*
// Dao Virtual Machine
// http://www.daovm.net
//
// Copyright (c) 2006-2013, Limin Fu
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED  BY THE COPYRIGHT HOLDERS AND  CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED  WARRANTIES,  INCLUDING,  BUT NOT LIMITED TO,  THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL  THE COPYRIGHT HOLDER OR CONTRIBUTORS  BE LIABLE FOR ANY DIRECT,
// INDIRECT,  INCIDENTAL, SPECIAL,  EXEMPLARY,  OR CONSEQUENTIAL  DAMAGES (INCLUDING,
// BUT NOT LIMITED TO,  PROCUREMENT OF  SUBSTITUTE  GOODS OR  SERVICES;  LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY OF
// LIABILITY,  WHETHER IN CONTRACT,  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<ctype.h>

#include"daoRegex.h"
#include"daoValue.h"

#define ALIGN_LEN   sizeof(void*)
#define ALIGN_MASK  (ALIGN_LEN-1)
#define ALIGN( x )  ((x+ALIGN_MASK) & ~ALIGN_MASK)


#ifdef WITHOUT_WCTYPE_H
#define iswalnum(c) isalnum(c)
#define iswalpha(c) isalpha(c)
#define iswascii(c) isascii(c)
#define iswblank(c) isblank(c)
#define iswcntrl(c) iscntrl(c)
#define iswdigit(c) isdigit(c)
#define iswgraph(c) isgraph(c)
#define iswhexnumber(c) ishexnumber(c)
#define iswideogram(c) isideogram(c)
#define iswlower(c) islower(c)
#define iswnumber(c) isnumber(c)
#define iswphonogram(c) isphonogram(c)
#define iswprint(c) isprint(c)
#define iswpunct(c) ispunct(c)
#define iswrune(c) isrune(c)
#define iswspace(c) isspace(c)
#define iswspecial(c) isspecial(c)
#define iswupper(c) isupper(c)
#define iswxdigit(c) isxdigit(c)
#endif


/* Regex Matching */
enum {
	PAT_NONE ,
	PAT_BEGIN ,
	PAT_STOP ,
	PAT_START ,
	PAT_END ,
	PAT_SPLIT ,
	PAT_JOIN ,
	PAT_BACKREF ,
	PAT_PATPAIR ,
	PAT_PAIR ,
	PAT_WORD ,
	PAT_SET ,
	PAT_ANY
};

enum DaoRegexConfig
{
	PAT_CONFIG_CASEINS = 1,
	PAT_CONFIG_MIN = 2
};
enum { PAT_ALL_FIXED = 1 };

const char *names[] =
{ "None", "Begin", "Stop", "Start", "End", "Split", "Join",
	"BackRef", "PatPair", "Pair", "Word", "Set", "Any" };

static void PrintRegex( DaoRegex *self, DaoRgxItem *pat )
{
	const char *type = "";
	const char *value = " ";
	char ch1[2] = {0,0};
	int i = 0, j = 0;
	if( pat->type > PAT_ANY ){
		type = "Class";
		ch1[0] = (char)pat->type;
		value = ch1;
	}else if( pat->type == PAT_SET || pat->type == PAT_WORD || pat->type == PAT_PAIR ){
		value = ((char*) self->wordbuf) + pat->word;
	}
	j = pat->word;
	i = pat->length;
	if( pat->type <= PAT_ANY ) type = names[ pat->type ];
	printf( "%6s  %3i,  %2i %2i,  %2i %2i,  %2i %1i,  %2i %2i, %s\n", type, pat->type,
			pat->min, pat->max, pat->next, pat->jump, pat->gid, pat->config, j, i, value );
}

static void SetWord( DaoRegex *self, DaoRgxItem *word, short m, wchar_t ch )
{
	if( word->type != PAT_WORD ){
		word->type = PAT_WORD;
		word->length = 0;
		word->word = m;
	}
	if( self->mbs ){
		char *mbs = ((char*) self->wordbuf) + word->word;
		mbs[ word->length ] = (char) ch;
		mbs[ ++ word->length ] = 0;
	}else{
		wchar_t *wcs = ((wchar_t*) self->wordbuf) + word->word;
		wcs[ word->length ] = ch;
		wcs[ ++ word->length ] = 0;
	}
}
static int RegexCompare( DaoRegex *self, DaoRgxItem *p1, DaoRgxItem *p2 )
{
	int res = 0;
	if( p2->type == p1->type && p1->type > PAT_JOIN ){
		if( p1->type >= PAT_ANY ){
			res = 1;
		}else if( p1->length == p2->length ){
			if( self->mbs ){
				char *mbs = (char*) self->wordbuf;
				res = (strncmp( mbs + p2->word, mbs + p1->word, p1->length ) ==0);
			}else{
				wchar_t *wcs = (wchar_t*) self->wordbuf;
				res = (wcsncmp( wcs + p2->word, wcs + p1->word, p1->length ) ==0);
			}
		}
	}
	return res;
}
static void PushWord( DaoRegex *self, DaoRgxItem *word )
{
	DaoRgxItem *patt = self->items + self->count -1;
	if( self->count && RegexCompare( self, word, patt ) ){
		patt->min ++;
		if( patt->max >=0 ) patt->max ++;
		word->type = PAT_NONE;
		return;
	}
	patt = self->items + self->count;
	memcpy( patt, word, sizeof(DaoRgxItem) );
	self->count ++;
	patt->config = self->config;
	patt->min = patt->max = 1;
	word->type = PAT_NONE;
}

static DaoRgxItem* PushRegex( DaoRegex *self, short type )
{
	DaoRgxItem *patt = self->items + self->count;
	patt->type = type;
	patt->gid = 0;
	patt->min = patt->max = patt->next = 1;
	patt->jump = patt->length = 0;
	patt->config = self->config;
	self->count ++;
	return patt;
}
static int ScanInteger( int *output, void *src, daoint pos, daoint end, int bl )
{
	char buf[10];
	char *mbs = ((char*)src) + pos;
	wchar_t ch, *wcs = ((wchar_t*)src) + pos;
	daoint i;
	*output = 0;
	if( end <= pos ) return 0;
	end -= pos;
	if( end >= 9 ) end = 9;
	for(i=0; i<end; i++){
		ch = bl ? mbs[i] : wcs[i];
		if( ! iswdigit( ch ) ) break;
		buf[i] = (char) ch;
		buf[i+1] = 0;
	}
	*output = atoi( buf );
	return i;
}
static int SkipSpace( void *src, daoint pos, daoint end, int bl )
{
	char *mbs = ((char*)src) + pos;
	wchar_t ch, *wcs = ((wchar_t*)src) + pos;
	daoint i;
	if( end <= pos ) return 0;
	end -= pos;
	for(i=0; i<end; i++){
		ch = bl ? mbs[i] : wcs[i];
		if( ! iswspace( ch ) ) break;
	}
	return i;
}
static int SetRepeat( short *omin, short *omax, void *src, DString *ds,
		daoint i, daoint end, int bl )
{
	short min=0, max=0, offset=1;
	char *mbs = (char*) src;
	wchar_t *wcs = (wchar_t*) src;
	wchar_t chi, chi2=0;
	daoint pos, pos2;
	int j, k, start = i;
	j = SkipSpace( src, i, end, bl );
	i += j;
	offset += j;
	chi = bl ? mbs[i] : wcs[i];
	if( i+1<end ) chi2 = bl ? mbs[i+1] : wcs[i+1];
	if( chi == '?' ){
		min = 0;
		max = 1;
	}else if( chi == '*' ){
		min = 0;
		max = -1;
	}else if( chi == '+' ){
		min = 1;
		max = -1;
	}else if( chi == '{' && chi2 != '{' ){
		pos = DString_BalancedChar( ds, '}', 0, 0, 0, i+1, end, 0 );
		pos2 = DString_BalancedChar( ds, '{', '{', '}', 0, i+1, pos, 0 );
		if( pos != MAXSIZE && pos2 == MAXSIZE ){
			j = SkipSpace( src, i+1, pos, bl );  i += j+1;
			j = ScanInteger( & k, src, i, pos, bl );  i += j;
			if( j ){
				j = SkipSpace( src, i, pos, bl );  i += j;
				min = max = k;
				chi = bl ? mbs[i] : wcs[i];
				if( chi == ',' ){
					j = SkipSpace( src, i+1, pos, bl );  i += j+1;
					j = ScanInteger( & k, src, i, pos, bl );
					max = j ? k : -1;
				}
				offset = pos - start + 1;
			}else{
				offset = 0;
			}
		}
	}else{
		offset = 0;
	}
	if( offset ){
		*omin = min;
		*omax = max;
	}
	return offset;
}
static int MakeRegex( DaoRegex *self, DString *ds, void *spatt,
		int start, int end, int grouping )
{
	DaoRgxItem *patt, *patt2, *split=NULL, *patts = self->items;
	DaoRgxItem word = { PAT_NONE };
	DaoRgxItem set = { PAT_NONE };
	wchar_t chi, chi2;
	wchar_t *wcs = (wchar_t*) spatt;
	char *mbs = (char*) spatt;
	int i, j, k, count=0, repeat=0, refgroup=0;
	short type, gid = self->indexed >0 ? self->indexed + 1E4 : self->group;
	daoint pos, pos2;
	int bl = self->mbs;
	int alts = DString_BalancedChar( ds, '|', '(', ')', '%', start, end, 1 );
	if( alts >0 || grouping >0 ){
		split = PushRegex( self, PAT_SPLIT );
		split->gid = gid;
		count = self->count;
	}
	self->indexed = 0;
	word.next = set.next = 1;
	word.jump = set.jump = 0;
	for(i=start; i<end; i++){
		int verbatim = 0;
		chi = bl ? mbs[i] : wcs[i];
		chi2 = (i+1) >= end ? 0 : (bl ? mbs[i+1] : wcs[i+1]);
		type = 0;
		k = i;
		/* space chars are ignored, to avoid this, write as [ ][\t][\n] */
		if( iswspace( chi ) ){
			if( word.type == PAT_WORD ) PushWord( self, & word );
			continue;
		}
		if( chi == '%' ){
			if( i+1 < end ) chi = bl ? mbs[i+1] : wcs[i+1];
			switch( chi ){
			case 's': case 'S': case 'k': case 'K': case 'p': case 'P':
			case 'c': case 'C': case 'a': case 'A': case 'w': case 'W':
			case 'd': case 'D': case 'x': case 'X':
				type = chi;
				break;
			case 't':
				type = PAT_WORD;
				SetWord( self, & word, i, L'\t' );
				break;
			case 'n':
				type = PAT_WORD;
				SetWord( self, & word, i, L'\n' );
				break;
			case '1': case '2': case '3': case '4': case '5':
			case '6': case '7': case '8': case '9':
				j = ScanInteger( & refgroup, spatt, i+1, end, bl );
				type = PAT_BACKREF;
				i += j-1;
				break;
			case 'b':
				type = PAT_NONE;
				if( i+3 < end ){
					chi = bl ? mbs[i+2] : wcs[i+2];
					chi2 = bl ? mbs[i+3] : wcs[i+3];
					if( ! iswspace( chi ) && ! iswspace( chi2 ) ){
						set.type = type = PAT_PAIR;
						set.word = i + 1;
						set.length = 2;
						if( bl ){
							char *buf = ((char*)self->wordbuf) + set.word;
							buf[0] = chi;  buf[1] = chi2;
						}else{
							wchar_t *buf = ((wchar_t*)self->wordbuf) + set.word;
							buf[0] = chi;  buf[1] = chi2;
						}
						i += 2;
					}
				}
				break;
			case 'B':
				type = PAT_NONE;
				chi = bl ? mbs[i+2] : wcs[i+2];
				if( chi == '{' ){
					pos = DString_BalancedChar( ds, '}','{','}','%', i+3, end, 0 );
					chi = 0;
					pos2 = MAXSIZE;
					if( pos != MAXSIZE ){
						chi = bl ? mbs[pos+1] : wcs[pos+1];
						pos2 = DString_BalancedChar( ds, '}','{','}','%', pos+2, end, 0 );
					}
					if( chi == '{' && pos2 != MAXSIZE ){
						short count2, count3;
						count2 = self->count;
						patt = PushRegex( self, PAT_PATPAIR );
						PushRegex( self, PAT_BEGIN );
						MakeRegex( self, ds, spatt, i+3, pos, 0 );
						PushRegex( self, PAT_STOP );
						count3 = self->count;
						PushRegex( self, PAT_BEGIN );
						MakeRegex( self, ds, spatt, pos+2, pos2, 0 );
						PushRegex( self, PAT_STOP );
						patt->next = self->count - count2;
						patt->length = count3 - count2;
						i = pos2;
						i += SetRepeat( & patt->min, & patt->max, spatt, ds, i+1, end, bl );
						continue;
					}
				}
				break;
			default :
				type = (i+1 < end) ? PAT_WORD : PAT_NONE;
				SetWord( self, & word, i, chi );
				break;
			}
			i ++;
		}else if( chi == '{' && chi2 == '{' ){ /* {{text}} */
			verbatim = 1;
			type = PAT_WORD;
			i += 2;
			while( i < end ){
				chi = bl ? mbs[i] : wcs[i];
				i += 1;
				/* printf( "%c\n", chi ); */
				if( chi == '}' ) break;
				SetWord( self, & word, i-1, chi );
			}
			chi = 0;
			k = i - 1;
			if( i < end ){
				chi = bl ? mbs[i] : wcs[i];
				k = i;
			}
			if( chi != '}' ) type = PAT_NONE;
		}else if( chi == '(' ){
			if( word.type == PAT_WORD ) PushWord( self, & word );
			pos = DString_BalancedChar( ds, ')', '(', ')', '%', i+1, end, 0 );
			if( pos != MAXSIZE ){
				self->group ++;
				MakeRegex( self, ds, spatt, i+1, pos, 1 );
				i = pos;
				continue;
			}
		}else if( chi == '[' ){
			pos = DString_BalancedChar( ds, ']', 0, 0, '%', i+1, end, 0 );
			pos2 = DString_BalancedChar( ds, '[', '[', ']', '%', i+1, end, 0 );
			if( pos == i+2 && pos2 == MAXSIZE ){
				type = PAT_WORD;
				i ++;
				chi = bl ? mbs[i] : wcs[i];
				SetWord( self, & word, i, chi );
				i = pos;
			}else if( pos != MAXSIZE && pos2 == MAXSIZE ){
				type = PAT_SET;
				set.type = PAT_SET;
				set.length = pos - i - 1;
				set.word = i + 1;
				if( bl ){
					char *buf = ((char*)self->wordbuf) + set.word;
					memcpy( buf, mbs + i + 1, set.length * sizeof(char) );
					buf[ set.length ] = 0;
				}else{
					wchar_t *buf = ((wchar_t*)self->wordbuf) + set.word;
					memcpy( buf, wcs + i + 1, set.length * sizeof(wchar_t) );
					buf[ set.length ] = 0;
				}
				i = pos;
			}else{
				type = PAT_NONE;
			}
		}else if( chi == '<' ){
			int k;
			if( word.type == PAT_WORD ) PushWord( self, & word );
			j = SkipSpace( spatt, i+1, end, bl );  i += j+1;
			chi = bl ? mbs[i] : wcs[i];
			k = i;
			if( iswdigit( chi ) ){
				j = ScanInteger( & k, spatt, i, end, bl );  i += j;
				self->indexed = k;
			}else if( iswlower( chi ) ){
				if( bl ){
					if( (bl && strncmp( mbs + i, "min", 3 ) ==0) ){
						self->config |= PAT_CONFIG_MIN;
						i += 3;
					}
				}else{
					if( (bl && wcsncmp( wcs + i, L"min", 3 ) ==0) ){
						self->config |= PAT_CONFIG_MIN;
						i += 3;
					}
				}
			}else{
				for(; i<end; i++){
					chi = bl ? mbs[i] : wcs[i];
					if( chi == 'I' ){
						self->config |= PAT_CONFIG_CASEINS;
					}else if( chi == 'C' ){
						self->config &= ~ PAT_CONFIG_CASEINS;
					}else{
						break;
					}
				}
			}
			j = SkipSpace( spatt, i, end, bl );  i += j;
			chi = bl ? mbs[i] : wcs[i];
			if( chi == '>' && k != i ){
				continue;
			}else{
				type = PAT_NONE;
			}
		}else if( chi == '.' ){
			type = PAT_ANY;
		}else if( chi == '|' ){
			if( word.type == PAT_WORD ) PushWord( self, & word );
			patt2 = PushRegex( self, PAT_JOIN );
			patt = PushRegex( self, PAT_SPLIT );
			patt->gid = patt2->gid = gid;
			if( split ) split->jump = patt - split;
			split = patt;
			continue;
		}else if( chi == '^' ){
			type = PAT_START;
		}else if( chi == '$' ){
			type = PAT_END;
		}else{
			type = PAT_NONE;
			switch( chi ){
			case ']': case ')': case '{': case '}': case '|':
			case '-': case '?': case '*': case '+': break;
			default : type = PAT_WORD; break;
			}
			if( type == PAT_WORD ) SetWord( self, & word, i, chi );
		}
		if( type != PAT_WORD && word.type == PAT_WORD ) PushWord( self, & word );
		patt = patts + self->count;
		patt->type = type;
		patt->gid = 0;
		patt->min = patt->max = 1;
		patt->next = 1;
		patt->jump = 0;
		if( type == PAT_BACKREF ) patt->gid = refgroup;
		if( type == PAT_NONE ) patt->length = k + 1; /* store the error position */
		if( word.type == PAT_WORD ){
			patt->word = word.word;
			patt->length = word.length;
		}else if( set.type == PAT_SET || set.type == PAT_PAIR ){
			patt->word = set.word;
			patt->length = set.length;
			set.type = PAT_NONE;
		}
		repeat = SetRepeat( & patt->min, & patt->max, spatt, ds, i+1, end, bl );
		i += repeat;
		if( patt->type == PAT_WORD && repeat && verbatim == 0 && patt->length > 1 ){
			/* Handle single character repetition: */
			int offset = patt->word;
			int length = patt->length;
			int min = patt->min;
			int max = patt->max;
			self->count += 1;
			patt->min = patt->max = 1;
			patt->length -= 1;
			patt = PushRegex( self, PAT_WORD );
			patt->min = min;
			patt->max = max;
			patt->word = offset + length - 1;
			patt->length = 1;
			self->count -= 1;
		}
		if( patt->type != PAT_WORD || repeat ){
			patt2 = NULL;
			if( self->count ) patt2 = patts + (self->count-1);
			if( patt2 && RegexCompare( self, patt, patt2 ) ){
				patt2->min += patt->min;
				if( patt2->max >=0 && patt->max >=0 ){
					patt2->max += patt->max;
				}else{
					patt2->max = -1;
				}
			}else{
				patt->config = self->config;
				self->count ++;
			}
			word.type = PAT_NONE;
		}
	}
	if( word.type == PAT_WORD ) PushWord( self, & word );
	if( alts >0 || grouping >0 ){
		patt = PushRegex( self, PAT_JOIN );
		patt->gid = gid;
		for(j=count; j<self->count; j++){
			patt = patts + j;
			if( patt->type == PAT_JOIN && patt->gid == gid )
				patt->next = self->count - j;
			if( patt->gid == gid && j+1 < self->count ){
				/* for PAT_SPLIT added for | in groups,
				 * required for calculate matching length */
				if( patt->type == PAT_SPLIT ) patt->gid = 0;
			}
		}
		split->length = self->count - count;
	}
	return self->count;
}

static const int sizepat = sizeof(DaoRegex);
static const int sizeitm = sizeof(DaoRgxItem);
static const int sizewch = sizeof(wchar_t);

static int InitRegex( DaoRegex *self, DString *ds )
{
	DaoRgxItem *patt;
	void *spatt = ds->mbs ? (void*)ds->mbs : (void*)ds->wcs;
	daoint i, j, max =0, end = ds->mbs ? ds->size : ds->size;
	int fixed = 1;
	self->indexed = self->count = self->group = self->config = self->attrib = 0;
	memset( self->items, 0, self->itemlen );
	memset( self->wordbuf, 0, self->wordlen );
	PushRegex( self, PAT_BEGIN );
	MakeRegex( self, ds, spatt, 0, end, 0 );
	PushRegex( self, PAT_STOP );
#if DEBUG
	if( self->count * sizeof(DaoRgxItem) > (size_t)self->itemlen ){
		printf( "error: allocated memory is not enough for the pattern.\n" );
		printf( "%s\n", ds->mbs );
		exit(0);
	}
#endif
	for(i=0; i<self->count; i++){
		patt = self->items + i;
		self->config |= patt->config;
		if( patt->type == PAT_SPLIT && patt->gid > max ) max = patt->gid;
		if( patt->min != patt->max ) fixed = 0;
		if( (patt->type == PAT_SET || patt->type == PAT_WORD) && (patt->config & PAT_CONFIG_CASEINS) ){
			if( self->mbs ){
				char *w = ((char*)self->wordbuf) + patt->word;
				for(j=0; j<patt->length; j++) w[j] = tolower( w[j] );
			}else{
				wchar_t *w = ((wchar_t*)self->wordbuf) + patt->word;
				for(j=0; j<patt->length; j++) w[j] = tolower( w[j] );
			}
		}
	}
	if( max > 10000 ){
		max -= 10000;
		for(i=0; i<self->count; i++){
			patt = self->items + i;
			if( patt->type == PAT_SPLIT || patt->type == PAT_JOIN ){
				if( patt->gid > 10000 ){
					patt->gid -= 10000;
				}else if( patt->gid ){
					patt->gid += max;
				}
			}
		}
		/* self->group += max; */
		self->group = max; /*  restrict capture exporting */
	}
	if( fixed ) self->attrib |= PAT_ALL_FIXED;
	memmove( self->items + self->count, self->wordbuf, self->wordlen );
	self->wordbuf = self->items + self->count;
	self->itemlen = self->count * sizeitm;
	self->length = sizepat + self->itemlen + self->wordlen;
	self->length = ALIGN( self->length );
	return self->count;
}

static int MatchOne( DaoRegex *self, DaoRgxItem *patt, daoint pos );

static int MatchWord( DaoRegex *self, DaoRgxItem *patt, daoint pos )
{
	short i;
	if( pos + patt->length > self->end ) return 0;
	if( self->mbs ){
		char *w = ((char*)self->wordbuf) + patt->word;
		char *s = ((char*) self->source) + pos;
		if( patt->config & PAT_CONFIG_CASEINS ){
			/* word is in lowercase */
			for(i=0; i<patt->length; i++) if( tolower( s[i] ) != w[i] ) return 0;
		}else{
			for(i=0; i<patt->length; i++) if( s[i] != w[i] ) return 0;
		}
	}else{
		wchar_t *w = ((wchar_t*)self->wordbuf) + patt->word;
		wchar_t *s = ((wchar_t*)self->source) + pos;
		if( self->config & PAT_CONFIG_CASEINS ){
			for(i=0; i<patt->length; i++) if( tolower( s[i] ) != w[i] ) return 0;
		}else{
			for(i=0; i<patt->length; i++) if( s[i] != w[i] ) return 0;
		}
	}
	patt->offset = patt->length;
	return 1;
}
static int MatchSet( DaoRegex *self, DaoRgxItem *patt, daoint pos )
{
	void *src = self->source;
	char *mbs = ((char*)self->wordbuf) + patt->word;
	wchar_t *wcs = ((wchar_t*)self->wordbuf) + patt->word;
	wchar_t ch, chi, chi2, chi3;
	int bl = self->mbs;
	int blmatch = 1;
	int matched = 0;
	short i;
	patt->offset = 0;
	if( pos >= self->end ) return 0;
	ch = bl ? ((char*)src)[pos] : ((wchar_t*)src)[pos];
	if( patt->config & PAT_CONFIG_CASEINS ) ch = tolower( ch );
	patt->offset = 1;
	for(i=0; i<patt->length; i++){
		chi3 = 0;
		if( bl ){
			chi = mbs[i];
			chi2 = mbs[i+1];
			if( i+2 < patt->length ) chi3 = mbs[i+2];
		}else{
			chi = wcs[i];
			chi2 = wcs[i+1];
			if( i+2 < patt->length ) chi3 = wcs[i+2];
		}
		if( i ==0 && chi == '^' ){
			blmatch = 0;
		}else if( chi == '%' ){
			i ++;
			switch( chi2 ){
			case 's': case 'S': case 'k': case 'K': case 'p': case 'P':
			case 'c': case 'C': case 'a': case 'A': case 'w': case 'W':
			case 'd': case 'D': case 'x': case 'X':
				patt->type = chi2;
				matched = (MatchOne( self, patt, pos ) !=0);
				patt->type = PAT_SET;
				break;
			case 't' : matched = (ch == L'\t'); break;
			case 'n' : matched = (ch == L'\n'); break;
			default  : matched = (ch == chi2); break;
			}
		}else if( iswalnum( chi ) && chi2 == '-' && iswalnum( chi3 ) ){
			i += 2;
			matched = ( ch >= chi && ch <= chi3 );
		}else{
			matched = (chi == ch);
		}
		if( matched ) break;
	}
	if( matched == blmatch ) return 1;
	patt->offset = 0;
	return 0;
}
static int GetGroup( DaoRegex *self, DaoRgxItem *p, int gid, daoint *start, daoint *end )
{
	daoint gp1, gp2;
	gp1 = gp2 = MAXSIZE;
	while( p->from >0 ){
		if( p->type == PAT_SPLIT && p->gid == gid ) gp1 = p->pos;
		if( p->type == PAT_JOIN && p->gid == gid ) gp2 = p->pos;
		p -= p->from;
		if( p->type == PAT_PATPAIR ){
			p = p + p->next - 1;
		}else if( p->type == PAT_BEGIN && p != self->items ){
			p --;
		}
	}
	if( gp1 == MAXSIZE || gp2 == MAXSIZE ) return 0;
	*start = gp1;
	*end = gp2;
	return 1;
}
static int MatchBackRef( DaoRegex *self, DaoRgxItem *patt, daoint pos )
{
	daoint i, n, gp1, gp2, end = self->end;
	short gid = patt->gid;
	patt->offset = 0;
	if( GetGroup( self, patt, gid, & gp1, & gp2 ) ==0 ) return 0;

	if( pos + (gp2 - gp1) > end ) return 0;
	n = gp2 - gp1;
	if( self->mbs ){
		char *s = ((char*)self->source) + pos;
		char *s2 = ((char*)self->source) + gp1;
		if( self->config & PAT_CONFIG_CASEINS ){
			for(i=0; i<n; i++) if( tolower( s[i] ) != tolower( s2[i] ) ) return 0;
		}else{
			for(i=0; i<n; i++) if( s[i] != s2[i] ) return 0;
		}
	}else{
		wchar_t *s = ((wchar_t*)self->source) + pos;
		wchar_t *s2 = ((wchar_t*)self->source) + gp1;
		if( self->config & PAT_CONFIG_CASEINS ){
			for(i=0; i<n; i++) if( tolower( s[i] ) != tolower( s2[i] ) ) return 0;
		}else{
			for(i=0; i<n; i++) if( s[i] != s2[i] ) return 0;
		}
	}
	patt->offset = gp2 - gp1;
	return 1;
}
static int FindPattern( DaoRegex *self, DaoRgxItem *patts, int npatt,
		void *src, daoint size, daoint *start, daoint *end, int fixed );

static int MatchPair( DaoRegex *self, DaoRgxItem *patt, daoint pos )
{
	daoint n = self->end - pos;
	daoint i=0;
	int nocase = ( patt->config & PAT_CONFIG_CASEINS );
	if( pos + 2 > self->end ) return 0;
	if( self->mbs ){
		char lc = ((char*)self->wordbuf)[patt->word];
		char rc = ((char*)self->wordbuf)[patt->word+1];
		char c, *s = ((char*) self->source) + pos;
		int count = 0;
		c = nocase ? tolower( *s ) : *s;
		if( c != lc ) return 0;
		for(i=0; i<n; i++){
			c = nocase ? tolower( s[i] ) : s[i];
			if( c == lc ){
				count ++;
			}else if( c == rc ){
				count --;
				if( count ==0 ) break;
			}
		}
	}else{
		wchar_t lc = ((wchar_t*)self->wordbuf)[patt->word];
		wchar_t rc = ((wchar_t*)self->wordbuf)[patt->word+1];
		wchar_t c, *s = ((wchar_t*)self->source) + pos;
		int count = 0;
		c = nocase ? tolower( *s ) : *s;
		if( c != lc ) return 0;
		for(i=0; i<n; i++){
			c = nocase ? towlower( s[i] ) : s[i];
			if( c == lc ){
				count ++;
			}else if( c == rc ){
				count --;
				if( count ==0 ) break;
			}
		}
	}
	if( i >= n ) return 0;
	patt->offset = i + 1;
	return 1;
}
static int CountRegex( DaoRegex *self, DaoRgxItem *patts, int npatt,
		void *src, daoint size, daoint start, daoint end, daoint start0, daoint end0 )
{
	daoint count = 0, m1 = start, m2 = end;
	while( FindPattern( self, patts, npatt, self->source, size, & m1, & m2, 0 ) ){
		if( (m2 - m1) == (end0 - start0) ){
			count += strncmp( src + m1, src + start0, m2 - m1 + 1 ) == 0;
		}
		m1 = m2;
		m2 = end;
	}
	return count;
}
static int MatchPatPair( DaoRegex *self, DaoRgxItem *patt, daoint pos )
{
	DaoRgxItem *pl = patt + 1;
	DaoRgxItem *pr = patt + patt->length;
	short itl = patt->length, itr = patt->next - patt->length;
	void *src = self->source;
	daoint end = self->end;
	daoint m1 = pos, m2 = end, m3 = pos, m4 = end, m5, m6;
	int bl;
	int count = 0;
	bl = FindPattern( self, pl, itl, src, end, & m1, & m2, 1 );
	/* printf( "bl = %i, %i %i\n", bl, m1, m2 ); */
	if( bl == 0 ) return 0;
	m3 = m2;
	m5 = m1;
	m6 = m2;
	bl = FindPattern( self, pr, itr, src, end, & m3, & m4, 0 );
	if( bl == 0 )  return 0;
	count = CountRegex( self, pl, itl, src, end, m2, m3, m1, m2 );
	/* printf( "count = %i, %i %i\n", count, m2, m3 ); */
	m2 = m4 + 1;
	while( count >0 ){
		/* reset for back references */
		FindPattern( self, pl, itl, src, end, & m5, & m6, 1 );
		m3 = m2;  m4 = end;
		bl = FindPattern( self, pr, itr, src, end, & m3, & m4, 0 );
		/* printf( "bl2 = %i, %i %i\n", bl, m3, m4 ); */
		if( bl == 0 )  return 0;
		count += CountRegex( self, pl, itl, src, end, m2, m3, m5, m6 ) - 1;
		/* printf( "count = %i, %i %i\n", count, m2, m3 ); */
		m2 = m4 + 1;
	}
	patt->offset = m4 + 1 - pos;
	/* correct backreference */
	m6 ++;
	m4 ++;
	/* printf( "%i  %i;  %i  %i\n", m5, m6, m3, m4 ); */
	FindPattern( self, pl, itl, src, end, & m5, & m6, 1 );
	FindPattern( self, pr, itr, src, end, & m3, & m4, 0 );
	return 1;
}
static int MatchOne( DaoRegex *self, DaoRgxItem *patt, daoint pos )
{
	patt->offset = 0;
	patt->count ++;
	switch( patt->type ){
	case PAT_SPLIT:
	case PAT_BEGIN: case PAT_JOIN: return 1;
	case PAT_START: return (pos == self->start);
	case PAT_END : return (pos >= self->end );
	case PAT_ANY : return (pos < self->end) && (++ patt->offset);
	case PAT_SET  : return MatchSet(  self, patt, pos );
	case PAT_WORD : return MatchWord( self, patt, pos );
	case PAT_PAIR : return MatchPair( self, patt, pos );
	case PAT_PATPAIR : return MatchPatPair( self, patt, pos );
	case PAT_BACKREF: return MatchBackRef( self, patt, pos );
	default : break;
	}
	if( pos >= self->end ) return 0;
	patt->offset = 1;
	if( self->mbs ){
		char ch = ((char*) self->source)[pos];
		switch( patt->type ){
		case 'a' : return isalpha( ch );
		case 's' : return isspace( ch );
		case 'k' : return iscntrl( ch );
		case 'p' : return ispunct( ch );
		case 'd' : return isdigit( ch );
		case 'x' : return isxdigit( ch );
		case 'c' : return islower( ch );
		case 'w' : return ( isalnum( ch ) || ch == '_' );
		case 'A' : return ! isalpha( ch );
		case 'S' : return ! isspace( ch );
		case 'K' : return ! iscntrl( ch );
		case 'P' : return ! ispunct( ch );
		case 'D' : return ! isdigit( ch );
		case 'X' : return ! isxdigit( ch );
		case 'C' : return isupper( ch );
		case 'W' : return ! ( isalnum( ch ) || ch == '_' );
		default : return 0;
		}
	}else{
		wchar_t ch = ((wchar_t*) self->source)[pos];
		switch( patt->type ){
		case 'a' : return iswalpha( ch );
		case 's' : return iswspace( ch );
		case 'k' : return iswcntrl( ch );
		case 'p' : return iswpunct( ch );
		case 'd' : return iswdigit( ch );
		case 'x' : return iswxdigit( ch );
		case 'c' : return iswlower( ch );
		case 'w' : return ( iswalnum( ch ) || ch == L'_' );
		case 'A' : return ! iswalpha( ch );
		case 'S' : return ! iswspace( ch );
		case 'K' : return ! iswcntrl( ch );
		case 'P' : return ! iswpunct( ch );
		case 'D' : return ! iswdigit( ch );
		case 'X' : return ! iswxdigit( ch );
		case 'C' : return iswupper( ch );
		case 'W' : return ! ( iswalnum( ch ) || ch == L'_' );
		default : return 0;
		}
	}
	return 0;
}

static int MatchMin( DaoRegex *self, DaoRgxItem *patt, daoint pos )
{
	patt->pos = pos;
	patt->count = 0;
	while( patt->count < patt->min ){
		if( MatchOne( self, patt, pos ) ==0 ) return 0;
		pos += patt->offset;
	}
	patt->offset = pos - patt->pos;
	return 1;
}
static int MatchExpand( DaoRegex *self, DaoRgxItem *patt, daoint pos )
{
	if( patt->max <0 || patt->count < patt->max ){
		daoint offset = patt->offset;
		if( MatchOne( self, patt, pos ) ){
			patt->offset += offset;
			return 1;
		}
	}
	return 0;
}

static int FindPattern( DaoRegex *self, DaoRgxItem *patts, int npatt,
		void *src, daoint size, daoint *start, daoint *end, int fixed )
{
	DaoRgxItem *patt, *patt2;
	daoint s1 = 0, s2 = size;
	daoint pos, sum, max = 0, min = 0x7fffffff, from = 0, to = size;
	daoint oldstart = self->start, oldend = self->end;
	int bl, expand, matched, minmode = ((self->config & PAT_CONFIG_MIN) !=0);
	if( patts == NULL ){
		patts = self->items;
		npatt = self->count;
	}
#if 0
	int i;
	printf( "npatt = %i %i\n", npatt, size );
	for(i=0; i<npatt; i++) printf( "%3i: ", i ), PrintRegex( self, patts + i );
#endif
	/* there is at least PAT_BEGIN and PAT_STOP */
	if( self->count <= 2 ) return 0;
	if( start && *start >= size ) return 0;
	if( start ) from = *start; else start = & s1;
	if( end ) to = *end + 1; else end = & s2;
	if( to > size ) to = size;
	self->source = src;
	self->start = from;
	self->end = to;
	pos = from;
	patt = patts;
	patt->pos = 0;
	patt->from = 0;
	bl = expand = matched = 0;
	while( pos < to+2 ){
		if( expand ){
			bl = MatchExpand( self, patt, pos );
		}else{
			bl = MatchMin( self, patt, pos );
		}
#if 0
		printf( "%4i  %2i, pos = %2i, o = %i,  ch = %c, e = %i, bl = %i, ",
				to, patts->pos, pos, patt->offset, ((char*)src)[pos], expand, bl );
		PrintRegex( self, patt );
#endif
		if( bl ==0 ){
			expand = 0;
			if( patt == patts ){
				pos += 1;
				if( fixed ) break;
			}else{
				pos = patt->pos;
				patt2 = patt - patt->from;
				if( patt2->jump && patt->from == patt2->next ){
					patt = patt2 + patt2->jump;
					patt->from = patt2->jump;
				}else{
					patt = patt2;
					expand = 1;
				}
			}
		}else{
			expand = 0;
			pos = patt->pos + patt->offset;
			patt2 = patt + patt->next;
			patt2->from = patt->next;
			patt = patt2;
		}
		if( matched && patt == patts ){
			break;
		}else if( patt->type == PAT_STOP ){
			patt->pos = pos;
			patt2 = patt;
			sum = pos - patts->pos;
			while( patt2->from >0 ){
				patt2 -= patt2->from;
				if( patt2->type == PAT_JOIN && patt2->gid ) sum += patt2->pos;
				if( patt2->type == PAT_SPLIT && patt2->gid ) sum -= patt2->pos;
			}
			if( patt->type == PAT_STOP && pos == patts->pos ){
				continue;
				pos += 1;
			}
			if( ( minmode && sum < min ) || ( ! minmode && sum >= max ) ){
				max = min = sum;
				*start = patts->pos;
				*end = pos - 1;
				patt2 = patt;
				patt2->posave = patt2->pos;
				patt2->fromsave = patt2->from;
				while( patt2->from >0 ){
					patt2 -= patt2->from;
					patt2->posave = patt2->pos;
					patt2->fromsave = patt2->from;
				}
			}
			patt2 = patts + npatt -1;
			patt = patt - patt->from;
			matched = 1;
			expand = 1;
			if( self->attrib & PAT_ALL_FIXED ) break;
		}
	}
	self->start = oldstart;
	self->end = oldend;
	return matched;
}

int DaoRegex_CheckSize( DString *src )
{
	int n = src->size;
	int m = DString_BalancedChar( src, '|', 0,0, '%', 0, n, 1 ) + 4; /* (|||) */
	int size = sizepat + (n+m) * sizeitm + n * (src->mbs ? 1 : sizewch) + 4;
	return ALIGN( size );
}
int DaoRegex_CheckSizeMBS( const char *src )
{
	DString str = DString_WrapMBS( src );
	return DaoRegex_CheckSize( & str );
}
int DaoRegex_CheckSizeWCS( const wchar_t *src )
{
	DString str = DString_WrapWCS( src );
	return DaoRegex_CheckSize( & str );
}

/* assuming "self" pointing to a memory,
 * enough large for the compiled pattern data */
void DaoRegex_Init( DaoRegex *self, DString *src )
{
	int n = src->size;
	int m = DString_BalancedChar( src, '|', 0,0, '%', 0, n, 1 ) + 4; /* (|||) */
	int size = DaoRegex_CheckSize( src );
	self->length = size;
	self->items = (DaoRgxItem*)(((char*)self) + sizepat);
	self->wordbuf = ((char*)self) + sizepat + (n+m) * sizeitm;
	self->mbs = (src->mbs != NULL);
	self->itemlen = (n+m) * sizeitm;
	self->wordlen = n * (src->mbs ? 1 : sizewch) + 1;
	InitRegex( self, src );
}

int DString_Match( DString *self, DString *patt, daoint *start, daoint *end )
{
	DaoRegex *regex;
	char *buf = NULL;
	int rc = 1;
	if( self->mbs && patt->wcs ) DString_ToMBS( patt );
	if( self->wcs && patt->mbs ) DString_ToWCS( patt );
	regex = (DaoRegex*) dao_malloc( DaoRegex_CheckSize( patt ) );
	DaoRegex_Init( regex, patt );
	if( self->mbs ){
		rc = FindPattern( regex, 0,0, self->mbs, self->size, start, end, 0 );
	}else{
		rc = FindPattern( regex, 0,0, self->wcs, self->size, start, end, 0 );
	}
	dao_free( buf );
	return rc;
}
int DString_MatchMBS( DString *self, const char *pat, daoint *start, daoint *end )
{
	DString *str = DString_New(1);
	DaoRegex *regex;
	int rc;
	DString_SetMBS( str, pat );
	if( self->wcs ) DString_ToWCS( str );
	regex = (DaoRegex*) dao_malloc( DaoRegex_CheckSize( str ) );
	DaoRegex_Init( regex, str );
	DString_Delete( str );
	rc = DaoRegex_Match( regex, self, start, end );
	dao_free( regex );
	return rc;
}
int DString_MatchWCS( DString *self, const wchar_t *pat, daoint *start, daoint *end )
{
	DString *str = DString_New(0);
	DaoRegex *regex;
	int rc;
	DString_SetWCS( str, pat );
	if( self->mbs ) DString_ToMBS( str );
	regex = (DaoRegex*) dao_malloc( DaoRegex_CheckSize( str ) );
	DaoRegex_Init( regex, str );
	DString_Delete( str );
	rc = DaoRegex_Match( regex, self, start, end );
	dao_free( regex );
	return rc;
}

int DString_ChangeMBS( DString *self, const char *pat, const char *target, int index )
{
	DString *str = DString_New(1);
	DString *tg = DString_New(1);
	DaoRegex *regex;
	int rc;
	DString_SetMBS( str, pat );
	DString_SetMBS( tg, target );
	if( self->wcs ){
		DString_ToWCS( str );
		DString_ToWCS( tg );
	}
	regex = (DaoRegex*) dao_malloc( DaoRegex_CheckSize( str ) );
	DaoRegex_Init( regex, str );
	DString_Delete( str );
	rc = DaoRegex_Change( regex, self, tg, index );
	DString_Delete( tg );
	dao_free( regex );
	return rc;
}
int DString_ChangeWCS( DString *self, const wchar_t *pat, const wchar_t *target, int index )
{
	DString *str = DString_New(0);
	DString *tg = DString_New(0);
	DaoRegex *regex;
	int rc;
	DString_SetWCS( str, pat );
	DString_SetWCS( tg, target );
	if( self->mbs ){
		DString_ToMBS( str );
		DString_ToMBS( tg );
	}
	regex = (DaoRegex*) dao_malloc( DaoRegex_CheckSize( str ) );
	DaoRegex_Init( regex, str );
	DString_Delete( str );
	rc = DaoRegex_Change( regex, self, tg, index );
	dao_free( regex );
	return rc;
}
DaoRegex* DaoRegex_New( DString *src )
{
	DaoRegex *self = NULL;
	int size = DaoRegex_CheckSize( src );
	self = (DaoRegex*) dao_malloc( size );
	self->length = size;
	DaoRegex_Init( self, src );
	return self;
}
void DaoRegex_Copy( DaoRegex *self, DaoRegex *src )
{
	memcpy( self, src, src->length );
	self->items = (DaoRgxItem*)(((char*)self) + sizepat);
	self->wordbuf = ((char*)self) + sizepat + self->itemlen;
}
int DaoRegex_Match( DaoRegex *self, DString *src, daoint *start, daoint *end )
{
	if( self->mbs != (src->mbs != NULL) ) return 0;
	if( self->mbs ){
		return FindPattern( self, 0,0, src->mbs, src->size, start, end, 0 );
	}else{
		return FindPattern( self, 0,0, src->wcs, src->size, start, end, 0 );
	}
	return 0;
}

int DaoRegex_SubMatch( DaoRegex *self, int gid, daoint *start, daoint *end )
{
	DaoRgxItem *p = self->items + self->count-1;
	daoint gp1, gp2;
	if( gid ==0 ){
		*start = self->items->posave;
		*end = p->posave -1;
		return 1;
	}
	gp1 = gp2 = MAXSIZE;
	while( p->fromsave >0 ){
		p -= p->fromsave;
		if( p->type == PAT_SPLIT && p->gid == gid ) gp1 = p->posave;
		if( p->type == PAT_JOIN && p->gid == gid ) gp2 = p->posave -1;
		if( p->type == PAT_PATPAIR ){
			p = p + p->next - 1;
		}else if( p->type == PAT_BEGIN && p != self->items ){
			p --;
		}
	}
	if( gp1 == MAXSIZE || gp2 == MAXSIZE ) return 0;
	*start = gp1;
	*end = gp2;
	return 1;
}
static void Dao_ParseTarget( DString *target, DArray *parts, DaoValue *sval )
{
	DString *tmp = sval->xString.data;
	DaoInteger ival = {DAO_INTEGER,0,0,0,0,0};
	daoint i, n = DString_Size( target );
	int ch, ch2;
	DString_Clear( tmp );
	for(i=0; i<n; i++){
		if( target->mbs ){
			ch = target->mbs[i];
			ch2 = target->mbs[i+1];
		}else{
			ch = target->wcs[i];
			ch2 = target->wcs[i+1];
		}
		if( ch == L'%' && iswdigit( ch2 ) ){
			DArray_PushBack( parts, sval );
			DString_Clear( tmp );
			ival.value = ch2 - L'0';
			DArray_PushBack( parts, (DaoValue*) & ival );
			i ++;
		}else if( ch == L'%' ){
			if( i+1 < n ){
				if( target->mbs ){
					DString_AppendChar( tmp, (char)ch2 );
				}else{
					DString_AppendWChar( tmp, ch2 );
				}
			}
			i ++;
		}else{
			if( target->mbs ){
				DString_AppendChar( tmp, (char)ch );
			}else{
				DString_AppendWChar( tmp, ch );
			}
		}
	}
	DArray_PushBack( parts, sval );
}
int DaoRegex_ChangeExt( DaoRegex *self, DString *source, DString *target,
		int index, daoint *start2, daoint *end2 )
{
	daoint start = start2 ? (daoint) *start2 : 0;
	daoint end = end2 ? (daoint) *end2 : 0;
	daoint i, n=0, p1=start, p2=end, p3, last;
	DaoValue *value = NULL;
	DaoString matched = {DAO_STRING,0,0,0,0,NULL};
	DString *tmp = DString_New( source->mbs != NULL );
	DString *replace = DString_New( source->mbs != NULL );
	DArray *array = DArray_New(D_VALUE);
	if( self ==NULL ) goto DoNothing;
	matched.data = tmp;
	Dao_ParseTarget( target, array, (DaoValue*) & matched );
	if( end == 0 ) p2 = end = DString_Size( source );
	n = last = 0;
	target = DString_Copy( target );
	while( DaoRegex_Match( self, source, & p1, & p2 ) ){
		n += 1;
		if( index ==0 || n == index ){
			DString_SubString( source, target, last, p1 - last );
			DString_Append( replace, target );
			DString_Clear( tmp );
			for(i=0; i<array->size; i++){
				value = array->items.pValue[i];
				if( value->type == DAO_INTEGER ){
					if( DaoRegex_SubMatch( self, value->xInteger.value, & p1, & p3 ) ){
						DString_SubString( source, target, p1, p3-p1 + 1 );
						DString_Append( tmp, target );
					}
				}else{
					DString_Append( tmp, value->xString.data );
				}
			}
			DString_Append( replace, tmp );
		}
		if( start2 ) *start2 = p1;
		if( end2 ) *end2 = p2;
		p1 = last = p2 + 1;
		p2 = end;
		if( index && n == index ) break;
	}
	DString_SubString( source, target, last, end - last );
	DString_Append( replace, target );
	DString_Assign( source, replace );
	DString_Delete( target );
DoNothing:
	DString_Delete( tmp );
	DString_Delete( replace );
	DArray_Delete( array );
	return n;
}
int DaoRegex_Change( DaoRegex *self, DString *source, DString *target, int index )
{
	return DaoRegex_ChangeExt( self, source, target, index, NULL, NULL );
}
#ifdef DAO_WITH_REGEX
int DaoRegex_MatchAndPack( DaoRegex *self, DString *source, DString *target,
		int index, int count, DArray *packs )
{
	daoint start = 0, end = 0;
	daoint i, n=0, p1=start, p2=end, p3;
	DaoValue *value = NULL;
	DaoString matched = {DAO_STRING,0,0,0,0,NULL};
	DString *tmp = DString_New( source->mbs != NULL );
	DString *tmp2 = DString_New( source->mbs != NULL );
	DArray *array = DArray_New(D_VALUE);
	if( self ==NULL ) goto DoNothing;
	matched.data = tmp;
	Dao_ParseTarget( target, array, (DaoValue*) & matched );
	if( end == 0 ) p2 = end = DString_Size( source );
	n = 0;
	while( DaoRegex_Match( self, source, & p1, & p2 ) ){
		n += 1;
		if( index ==0 || n == index ){
			DString_Clear( tmp );
			for(i=0; i<array->size; i++){
				value = array->items.pValue[i];
				if( value->type == DAO_INTEGER ){
					if( DaoRegex_SubMatch( self, value->xInteger.value, & p1, & p3 ) ){
						DString_SubString( source, tmp2, p1, p3-p1 + 1 );
						DString_Append( tmp, tmp2 );
					}
				}else{
					DString_Append( tmp, value->xString.data );
				}
			}
			DArray_Append( packs, (DaoValue*) & matched );
		}
		p1 = p2 + 1;
		p2 = end;
		if( index && n == index ) break;
		if( count && packs->size >= count ) break;
	}
DoNothing:
	DString_Delete( tmp );
	DString_Delete( tmp2 );
	DArray_Delete( array );
	return n;
}
#endif
