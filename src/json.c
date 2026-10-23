#include "json.h"

#include <string.h>

#define JSON_MAX_NODES 8192
#define JSON_POOL_SIZE (384 * 1024)

static JsonValue gNodes[JSON_MAX_NODES];
static long gNodeCount;
static char gPool[JSON_POOL_SIZE];
static long gPoolUsed;

typedef struct {
    char *p;
    char *end;
    JsonValue *root;
    int error;
} Parser;

static void SkipWs(Parser *ps)
{
    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            ps->p++;
        else
            break;
    }
}

static JsonValue *NewNode(unsigned char type)
{
    JsonValue *v;
    if (gNodeCount >= JSON_MAX_NODES) {
        return 0;
    }
    v = &gNodes[gNodeCount++];
    memset(v, 0, sizeof(*v));
    v->type = type;
    return v;
}

static char *PoolCopy(const char *s, long len)
{
    char *out;
    if (gPoolUsed + len + 1 > JSON_POOL_SIZE)
        return 0;
    out = gPool + gPoolUsed;
    memcpy(out, s, len);
    out[len] = 0;
    gPoolUsed += len + 1;
    return out;
}

static int HexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static char *ParseStringRaw(Parser *ps)
{
    char *start;
    char *out;
    long max;
    long n = 0;

    if (ps->p >= ps->end || *ps->p != '"') {
        ps->error = 1;
        return 0;
    }
    ps->p++;
    start = ps->p;
    max = ps->end - start;
    out = gPool + gPoolUsed;
    if (gPoolUsed + max + 1 > JSON_POOL_SIZE) {
        ps->error = 1;
        return 0;
    }
    while (ps->p < ps->end) {
        unsigned char c = (unsigned char)*ps->p++;
        if (c == '"') {
            out[n] = 0;
            gPoolUsed += n + 1;
            return out;
        }
        if (c == '\\') {
            if (ps->p >= ps->end)
                break;
            c = (unsigned char)*ps->p++;
            switch (c) {
            case '"': out[n++] = '"'; break;
            case '\\': out[n++] = '\\'; break;
            case '/': out[n++] = '/'; break;
            case 'b': out[n++] = '\b'; break;
            case 'f': out[n++] = '\f'; break;
            case 'n': out[n++] = '\n'; break;
            case 'r': out[n++] = '\r'; break;
            case 't': out[n++] = '\t'; break;
            case 'u': {
                int h0, h1, h2, h3;
                unsigned long cp;
                if (ps->end - ps->p < 4)
                    break;
                h0 = HexDigit(ps->p[0]);
                h1 = HexDigit(ps->p[1]);
                h2 = HexDigit(ps->p[2]);
                h3 = HexDigit(ps->p[3]);
                if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0)
                    break;
                ps->p += 4;
                cp = (unsigned long)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                if (cp < 0x80) {
                    out[n++] = (char)cp;
                } else if (cp < 0x800) {
                    out[n++] = (char)(0xC0 | (cp >> 6));
                    out[n++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    out[n++] = (char)(0xE0 | (cp >> 12));
                    out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    out[n++] = (char)(0x80 | (cp & 0x3F));
                }
                break;
            }
            default:
                out[n++] = (char)c;
                break;
            }
        } else {
            out[n++] = (char)c;
        }
    }
    ps->error = 1;
    return 0;
}

static JsonValue *ParseValue(Parser *ps);

static JsonValue *ParseArray(Parser *ps)
{
    JsonValue *arr;
    JsonValue *last = 0;

    ps->p++; /* [ */
    arr = NewNode(JSON_ARRAY);
    if (!arr) {
        ps->error = 1;
        return 0;
    }
    SkipWs(ps);
    if (ps->p < ps->end && *ps->p == ']') {
        ps->p++;
        return arr;
    }
    while (ps->p < ps->end) {
        JsonValue *v = ParseValue(ps);
        if (!v) {
            ps->error = 1;
            return arr;
        }
        if (last)
            last->next = v;
        else
            arr->u.first = v;
        last = v;
        SkipWs(ps);
        if (ps->p < ps->end && *ps->p == ',') {
            ps->p++;
            SkipWs(ps);
            continue;
        }
        if (ps->p < ps->end && *ps->p == ']') {
            ps->p++;
            return arr;
        }
        ps->error = 1;
        return arr;
    }
    ps->error = 1;
    return arr;
}

static JsonValue *ParseObject(Parser *ps)
{
    JsonValue *obj;
    JsonValue *last = 0;

    ps->p++; /* { */
    obj = NewNode(JSON_OBJECT);
    if (!obj) {
        ps->error = 1;
        return 0;
    }
    SkipWs(ps);
    if (ps->p < ps->end && *ps->p == '}') {
        ps->p++;
        return obj;
    }
    while (ps->p < ps->end) {
        char *key;
        JsonValue *v;
        SkipWs(ps);
        key = ParseStringRaw(ps);
        if (!key) {
            ps->error = 1;
            return obj;
        }
        SkipWs(ps);
        if (ps->p >= ps->end || *ps->p != ':') {
            ps->error = 1;
            return obj;
        }
        ps->p++;
        v = ParseValue(ps);
        if (!v) {
            ps->error = 1;
            return obj;
        }
        v->key = key;
        if (last)
            last->next = v;
        else
            obj->u.first = v;
        last = v;
        SkipWs(ps);
        if (ps->p < ps->end && *ps->p == ',') {
            ps->p++;
            continue;
        }
        if (ps->p < ps->end && *ps->p == '}') {
            ps->p++;
            return obj;
        }
        ps->error = 1;
        return obj;
    }
    ps->error = 1;
    return obj;
}

static JsonValue *ParseValue(Parser *ps)
{
    JsonValue *v;

    SkipWs(ps);
    if (ps->p >= ps->end) {
        ps->error = 1;
        return 0;
    }

    switch (*ps->p) {
    case '{':
        return ParseObject(ps);
    case '[':
        return ParseArray(ps);
    case '"':
        v = NewNode(JSON_STRING);
        if (!v)
            break;
        v->u.string = ParseStringRaw(ps);
        if (!v->u.string) {
            ps->error = 1;
            return 0;
        }
        return v;
    case 't':
        if (ps->end - ps->p >= 4 && memcmp(ps->p, "true", 4) == 0) {
            ps->p += 4;
            v = NewNode(JSON_BOOL);
            if (v)
                v->u.boolean = 1;
            return v;
        }
        break;
    case 'f':
        if (ps->end - ps->p >= 5 && memcmp(ps->p, "false", 5) == 0) {
            ps->p += 5;
            v = NewNode(JSON_BOOL);
            if (v)
                v->u.boolean = 0;
            return v;
        }
        break;
    case 'n':
        if (ps->end - ps->p >= 4 && memcmp(ps->p, "null", 4) == 0) {
            ps->p += 4;
            return NewNode(JSON_NULL);
        }
        break;
    default:
        break;
    }

    if (*ps->p == '-' || (*ps->p >= '0' && *ps->p <= '9')) {
        long sign = 1;
        long num = 0;
        if (*ps->p == '-') {
            sign = -1;
            ps->p++;
        }
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') {
            num = num * 10 + (*ps->p - '0');
            ps->p++;
        }
        if (ps->p < ps->end && *ps->p == '.') {
            ps->p++;
            while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9')
                ps->p++;
        }
        if (ps->p < ps->end && (*ps->p == 'e' || *ps->p == 'E')) {
            ps->p++;
            if (ps->p < ps->end && (*ps->p == '+' || *ps->p == '-'))
                ps->p++;
            while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9')
                ps->p++;
        }
        v = NewNode(JSON_NUMBER);
        if (v)
            v->u.number = num * sign;
        return v;
    }

    ps->error = 1;
    return 0;
}

JsonValue *JsonParse(char *text, long length)
{
    Parser ps;
    gNodeCount = 0;
    gPoolUsed = 0;
    ps.p = text;
    ps.end = text + length;
    ps.root = 0;
    ps.error = 0;
    ps.root = ParseValue(&ps);
    if (ps.error)
        return 0;
    return ps.root;
}

JsonValue *JsonGet(JsonValue *object, const char *key)
{
    JsonValue *v;
    if (!object || object->type != JSON_OBJECT)
        return 0;
    for (v = object->u.first; v; v = v->next) {
        if (v->key && strcmp(v->key, key) == 0)
            return v;
    }
    return 0;
}

JsonValue *JsonFirst(JsonValue *array)
{
    if (!array || array->type != JSON_ARRAY)
        return 0;
    return array->u.first;
}

const char *JsonString(JsonValue *value)
{
    if (!value || value->type != JSON_STRING)
        return "";
    return value->u.string;
}

long JsonNumber(JsonValue *value)
{
    if (!value)
        return 0;
    if (value->type == JSON_NUMBER)
        return value->u.number;
    if (value->type == JSON_BOOL)
        return value->u.boolean;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Streaming parser for Jellyfin /Items responses                     */
/* ------------------------------------------------------------------ */

static void SkipSpace(char **pp, char *end)
{
    while (*pp < end) {
        char c = **pp;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            (*pp)++;
        else
            break;
    }
}

static int ParseStringInto(char **pp, char *end, char *dst, long max)
{
    long n = 0;
    if (*pp >= end || **pp != '"')
        return 0;
    (*pp)++;
    while (*pp < end) {
        unsigned char c = (unsigned char)*(*pp)++;
        if (c == '"') {
            if (dst && max > 0)
                dst[n < max ? n : max - 1] = 0;
            return 1;
        }
        if (c == '\\') {
            if (*pp >= end)
                return 0;
            c = (unsigned char)*(*pp)++;
            switch (c) {
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u': {
                int h0, h1, h2, h3;
                unsigned long cp;
                if (end - *pp < 4)
                    return 0;
                h0 = HexDigit((*pp)[0]);
                h1 = HexDigit((*pp)[1]);
                h2 = HexDigit((*pp)[2]);
                h3 = HexDigit((*pp)[3]);
                if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0)
                    return 0;
                *pp += 4;
                cp = (unsigned long)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                if (cp < 0x80) {
                    c = (unsigned char)cp;
                } else {
                    /* Keep non-ASCII as UTF-8; the UI treats it as bytes. */
                    if (dst && n + 3 < max) {
                        if (cp < 0x800) {
                            dst[n++] = (char)(0xC0 | (cp >> 6));
                            dst[n++] = (char)(0x80 | (cp & 0x3F));
                        } else {
                            dst[n++] = (char)(0xE0 | (cp >> 12));
                            dst[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            dst[n++] = (char)(0x80 | (cp & 0x3F));
                        }
                    }
                    continue;
                }
                break;
            }
            default: break;
            }
        }
        if (dst && n < max - 1)
            dst[n++] = (char)c;
    }
    return 0;
}

static int SkipValue(char **pp, char *end)
{
    SkipSpace(pp, end);
    if (*pp >= end)
        return 0;
    if (**pp == '"')
        return ParseStringInto(pp, end, 0, 0);
    if (**pp == '{' || **pp == '[') {
        int depth = 1;
        (*pp)++;
        while (*pp < end && depth > 0) {
            char c = **pp;
            if (c == '"') {
                if (!ParseStringInto(pp, end, 0, 0))
                    return 0;
                continue;
            }
            if (c == '{' || c == '[')
                depth++;
            else if (c == '}' || c == ']')
                depth--;
            (*pp)++;
        }
        return depth == 0;
    }
    while (*pp < end) {
        char c = **pp;
        if (c == ',' || c == '}' || c == ']')
            break;
        (*pp)++;
    }
    return 1;
}

static long ParseLongValue(char **pp, char *end)
{
    long sign = 1;
    long value = 0;
    SkipSpace(pp, end);
    if (*pp < end && **pp == '-') {
        sign = -1;
        (*pp)++;
    }
    while (*pp < end && **pp >= '0' && **pp <= '9') {
        value = value * 10 + (**pp - '0');
        (*pp)++;
    }
    if (*pp < end && **pp == '.') {
        (*pp)++;
        while (*pp < end && **pp >= '0' && **pp <= '9')
            (*pp)++;
    }
    if (*pp < end && (**pp == 'e' || **pp == 'E')) {
        (*pp)++;
        if (*pp < end && (**pp == '+' || **pp == '-'))
            (*pp)++;
        while (*pp < end && **pp >= '0' && **pp <= '9')
            (*pp)++;
    }
    return value * sign;
}

/* RunTimeTicks is in 100 ns units and does not fit in a signed 32-bit long
 * for tracks longer than about 3.5 minutes.  Keep just the whole seconds. */
static long ParseSecondsFromTicks(char **pp, char *end)
{
    char digits[32];
    int n = 0;
    int i;
    long seconds = 0;

    SkipSpace(pp, end);
    if (*pp >= end || (**pp < '0' || **pp > '9')) {
        SkipValue(pp, end);
        return 0;
    }
    while (*pp < end && **pp >= '0' && **pp <= '9') {
        if (n < (int)sizeof(digits))
            digits[n++] = **pp;
        (*pp)++;
    }
    if (*pp < end && **pp == '.') {
        (*pp)++;
        while (*pp < end && **pp >= '0' && **pp <= '9')
            (*pp)++;
    }
    if (*pp < end && (**pp == 'e' || **pp == 'E')) {
        (*pp)++;
        if (*pp < end && (**pp == '+' || **pp == '-'))
            (*pp)++;
        while (*pp < end && **pp >= '0' && **pp <= '9')
            (*pp)++;
    }
    if (n <= 7)
        return 0;
    if (n > (int)sizeof(digits))
        n = (int)sizeof(digits);
    for (i = 0; i < n - 7; i++) {
        seconds = seconds * 10 + (digits[i] - '0');
        if (seconds > 1000000L)
            seconds = 1000000L;
    }
    return seconds;
}

static long ParseIntOrNull(char **pp, char *end)
{
    SkipSpace(pp, end);
    if (*pp >= end || ((**pp < '0' || **pp > '9') && **pp != '-')) {
        SkipValue(pp, end);
        return 0;
    }
    return ParseLongValue(pp, end);
}

static int ParseStringOrNull(char **pp, char *end, char *dst, long max)
{
    SkipSpace(pp, end);
    if (*pp < end && **pp == '"')
        return ParseStringInto(pp, end, dst, max);
    return SkipValue(pp, end);
}

static void ParseArtists(char **pp, char *end, char *dst, long max)
{
    SkipSpace(pp, end);
    if (*pp >= end || **pp != '[') {
        SkipValue(pp, end);
        return;
    }
    (*pp)++;
    SkipSpace(pp, end);
    while (*pp < end && **pp != ']') {
        if (**pp == '"') {
            if (dst && dst[0] == 0)
                ParseStringInto(pp, end, dst, max);
            else
                ParseStringInto(pp, end, 0, 0);
        } else {
            SkipValue(pp, end);
        }
        SkipSpace(pp, end);
        if (*pp < end && **pp == ',') {
            (*pp)++;
            SkipSpace(pp, end);
        }
    }
    if (*pp < end && **pp == ']')
        (*pp)++;
}

static int ParseItem(char **pp, char *end, char *name, long nameMax,
                     char *id, long idMax, char *artist, long artistMax,
                     char *albumArtist, long albumArtistMax,
                     char *album, long albumMax, long *seconds,
                     long *trackNo, long *discNo)
{
    SkipSpace(pp, end);
    if (*pp >= end || **pp != '{')
        return 0;
    (*pp)++;
    name[0] = id[0] = artist[0] = albumArtist[0] = album[0] = 0;
    *seconds = 0;
    *trackNo = 0;
    *discNo = 0;

    for (;;) {
        char key[48];
        SkipSpace(pp, end);
        if (*pp >= end)
            return 0;
        if (**pp == '}') {
            (*pp)++;
            return 1;
        }
        if (!ParseStringInto(pp, end, key, sizeof(key)))
            return 0;
        SkipSpace(pp, end);
        if (*pp >= end || **pp != ':')
            return 0;
        (*pp)++;
        SkipSpace(pp, end);

        if (strcmp(key, "Name") == 0) {
            if (!ParseStringOrNull(pp, end, name, nameMax))
                return 0;
        } else if (strcmp(key, "Id") == 0) {
            if (!ParseStringOrNull(pp, end, id, idMax))
                return 0;
        } else if (strcmp(key, "Album") == 0) {
            if (!ParseStringOrNull(pp, end, album, albumMax))
                return 0;
        } else if (strcmp(key, "AlbumArtist") == 0) {
            if (!ParseStringOrNull(pp, end, albumArtist, albumArtistMax))
                return 0;
        } else if (strcmp(key, "AlbumArtists") == 0) {
            if (albumArtist[0] == 0)
                ParseArtists(pp, end, albumArtist, albumArtistMax);
            else
                ParseArtists(pp, end, NULL, 0);
        } else if (strcmp(key, "Artists") == 0) {
            ParseArtists(pp, end, artist, artistMax);
        } else if (strcmp(key, "RunTimeTicks") == 0) {
            *seconds = ParseSecondsFromTicks(pp, end);
        } else if (strcmp(key, "IndexNumber") == 0) {
            *trackNo = ParseIntOrNull(pp, end);
        } else if (strcmp(key, "ParentIndexNumber") == 0) {
            *discNo = ParseIntOrNull(pp, end);
        } else {
            if (!SkipValue(pp, end))
                return 0;
        }

        SkipSpace(pp, end);
        if (*pp < end && **pp == ',') {
            (*pp)++;
            continue;
        }
        if (*pp < end && **pp == '}') {
            (*pp)++;
            return 1;
        }
        return 0;
    }
}

long JsonParseItems(char *text, long length, JsonTrackCallback callback,
                    void *ctx, long maxTracks)
{
    char *p = text;
    char *end = text + length;
    char name[80], id[48], artist[80], albumArtist[80], album[80];
    long count = 0;

    p = strstr(text, "\"Items\"");
    if (!p)
        return 0;
    p += 7;
    while (p < end && *p != '[')
        p++;
    if (p >= end || *p != '[')
        return 0;
    p++;
    for (;;) {
        long seconds = 0;
        long trackNo = 0;
        long discNo = 0;
        SkipSpace(&p, end);
        if (p >= end)
            break;
        if (*p == ']')
            break;
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '{') {
            if (!SkipValue(&p, end))
                break;
            continue;
        }
        if (!ParseItem(&p, end, name, sizeof(name), id, sizeof(id),
                       artist, sizeof(artist),
                       albumArtist, sizeof(albumArtist),
                       album, sizeof(album), &seconds,
                       &trackNo, &discNo))
            break;
        if (name[0] && id[0] && count < maxTracks) {
            callback(name, id, artist, albumArtist, album, seconds,
                     trackNo, discNo, ctx);
            count++;
        }
        SkipSpace(&p, end);
        if (p < end && *p == ',')
            p++;
        else if (p < end && *p == ']')
            break;
        else
            break;
    }
    return count;
}
