/*+===================================================================
  File:      movie_fixture.h

  Summary:   Original three-frame raw RGB movie. No third-party media assets.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Original three-frame raw RGB movie. No third-party media assets. */
static unsigned char movie_bytes[2048];
static unsigned movie_used;

static void movie_put(unsigned offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        movie_bytes[offset + i] = value >> (24 - 8 * i);
}

static unsigned movie_begin(const char* type)
{
    unsigned start = movie_used;
    movie_used += 8;
    memcpy(movie_bytes + start + 4, type, 4);
    return start;
}

static void movie_end(unsigned start)
{
    movie_put(start, movie_used - start);
}

static void movie_fixture(void)
{
    memset(movie_bytes, 0, sizeof movie_bytes);
    movie_used = 0;
    unsigned mdat = movie_begin("mdat");
    for (unsigned sample = 0; sample < 3; ++sample)
        for (unsigned pixel = 0; pixel < 4; ++pixel)
            for (unsigned color = 0; color < 3; ++color)
                movie_bytes[movie_used++] = sample == color ? 255 : 0;
    movie_end(mdat);
    unsigned moov = movie_begin("moov"), trak = movie_begin("trak");
    unsigned mdia = movie_begin("mdia"), atom = movie_begin("mdhd");
    movie_put(movie_used + 12, 60);
    movie_put(movie_used + 16, 90);
    movie_used += 24;
    movie_end(atom);
    atom = movie_begin("hdlr");
    memcpy(movie_bytes + movie_used + 8, "vide", 4);
    movie_used += 24;
    movie_end(atom);
    unsigned minf = movie_begin("minf"), stbl = movie_begin("stbl");
    atom = movie_begin("stsd");
    movie_put(movie_used + 4, 1);
    movie_used += 8;
    movie_put(movie_used, 86);
    memcpy(movie_bytes + movie_used + 4, "raw ", 4);
    movie_bytes[movie_used + 15] = 1;
    movie_bytes[movie_used + 33] = movie_bytes[movie_used + 35] = 2;
    movie_bytes[movie_used + 83] = 24;
    movie_used += 86;
    movie_end(atom);
    atom = movie_begin("stts");
    movie_put(movie_used + 4, 1);
    movie_put(movie_used + 8, 3);
    movie_put(movie_used + 12, 30);
    movie_used += 16;
    movie_end(atom);
    atom = movie_begin("stsc");
    movie_put(movie_used + 4, 1);
    movie_put(movie_used + 8, 1);
    movie_put(movie_used + 12, 3);
    movie_put(movie_used + 16, 1);
    movie_used += 20;
    movie_end(atom);
    atom = movie_begin("stsz");
    movie_put(movie_used + 4, 12);
    movie_put(movie_used + 8, 3);
    movie_used += 12;
    movie_end(atom);
    atom = movie_begin("stco");
    movie_put(movie_used + 4, 1);
    movie_put(movie_used + 8, 8);
    movie_used += 12;
    movie_end(atom);
    movie_end(stbl);
    movie_end(minf);
    movie_end(mdia);
    movie_end(trak);
    movie_end(moov);
}
