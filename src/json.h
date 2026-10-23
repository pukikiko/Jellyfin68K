/* Minimal JSON parser for the Jellyfin 68K client. */
#ifndef JF_JSON_H
#define JF_JSON_H

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;
struct JsonValue {
    unsigned char type;
    JsonValue *next;          /* sibling in array/object */
    char *key;                /* object member name, NUL terminated */
    union {
        long boolean;
        long number;
        char *string;
        JsonValue *first;     /* first child for arrays/objects */
    } u;
};

JsonValue *JsonParse(char *text, long length);
JsonValue *JsonGet(JsonValue *object, const char *key);
JsonValue *JsonFirst(JsonValue *array);
const char *JsonString(JsonValue *value);
long JsonNumber(JsonValue *value);

/* Streaming parser for Jellyfin's /Items response.  It never builds a
 * tree, so it can handle several hundred items in a small heap. */
typedef void (*JsonTrackCallback)(const char *name, const char *id,
                                  const char *artist,
                                  const char *albumArtist,
                                  const char *album, long seconds,
                                  long trackNo, long discNo, void *ctx);

long JsonParseItems(char *text, long length, JsonTrackCallback callback,
                    void *ctx, long maxTracks);

#endif
