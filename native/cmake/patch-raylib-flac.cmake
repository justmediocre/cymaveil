# raylib 6.0: UnloadMusicStream double-frees FLAC decoders. drflac_close()
# already frees the object; the drflac_free() after it corrupts the heap.
# Run as PATCH_COMMAND from the raylib source dir. Idempotent.
file(READ src/raudio.c CONTENT)
string(REPLACE
    "{ drflac_close((drflac *)music.ctxData); drflac_free((drflac *)music.ctxData, NULL); }"
    "{ drflac_close((drflac *)music.ctxData); }"
    CONTENT "${CONTENT}")
file(WRITE src/raudio.c "${CONTENT}")
