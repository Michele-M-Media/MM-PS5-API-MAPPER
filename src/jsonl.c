#include "mm_mapper.h"
#include <string.h>
#include <errno.h>

void mm_json_string(FILE *fp, const unsigned char *s, size_t len) {
    fputc('"', fp);
    for (size_t i=0; i<len; ++i) {
        unsigned char c=s[i];
        switch (c) {
            case '"': fputs("\\\"",fp); break;
            case '\\': fputs("\\\\",fp); break;
            case '\b': fputs("\\b",fp); break;
            case '\f': fputs("\\f",fp); break;
            case '\n': fputs("\\n",fp); break;
            case '\r': fputs("\\r",fp); break;
            case '\t': fputs("\\t",fp); break;
            default:
                if (c < 0x20 || c >= 0x7f) fprintf(fp,"\\u%04x",(unsigned)c);
                else fputc((int)c,fp);
        }
    }
    fputc('"',fp);
}

void mm_json_cstr(FILE *fp, const char *s) {
    if (!s) { fputs("null", fp); return; }
    mm_json_string(fp, (const unsigned char*)s, strlen(s));
}

void mm_emit_error(mm_jsonl_t *out, const char *path, const char *stage, const char *message, int errnum) {
    if (!out || !out->fp) return;
    fputs("{\"record\":\"error\",\"path\":",out->fp); mm_json_cstr(out->fp,path);
    fputs(",\"stage\":",out->fp); mm_json_cstr(out->fp,stage);
    fputs(",\"message\":",out->fp); mm_json_cstr(out->fp,message);
    fprintf(out->fp,",\"errno\":%d}\n",errnum);
    fflush(out->fp);
}
