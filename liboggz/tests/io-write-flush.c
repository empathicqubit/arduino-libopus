/*
   Copyright (C) 2003 Commonwealth Scientific and Industrial Research
   Organisation (CSIRO) Australia

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   - Neither the name of CSIRO Australia nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
   PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE ORGANISATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "oggz.h"


#include "oggz_tests.h"

/* #define DEBUG */

#define DATA_BUF_LEN 1024

static long serialno1, serialno2;
static int newpage = 0;

static int hungry_iter = 0;
static int hungry_e_o_s = 0;

static int read_iter = 0;
static int read_e_o_s = 0;

static int write_called = 0;
static int write_offset = 0;


static int
hungry (OGGZ * oggz, int empty, void * user_data)
{
  int flush = *((int *)user_data);
  unsigned char buf[1];
  ogg_packet op;
  long serialno;

  if (hungry_iter > 21) return 1;

  buf[0] = 'a' + hungry_iter/2;
  serialno = (hungry_iter%2) ? serialno1 : serialno2;

  op.packet = buf;
  op.bytes = 1;
  op.b_o_s = -1;
  op.e_o_s = hungry_e_o_s;
  op.granulepos = hungry_iter/2;
  op.packetno = hungry_iter/2;

  /* Main check */
  if (oggz_write_feed (oggz, &op, serialno, flush, NULL) != 0)
    FAIL ("Oggz write failed");

  hungry_iter++;
  if (hungry_iter >= 20) hungry_e_o_s = 1;
  
  return 0;
}

static int
read_page (OGGZ * oggz, const ogg_page * og, long serialno, void * user_data)
{
  if (newpage != 0) FAIL ("New page found where none is needed");
  newpage = 1;

  return 0;
}

static int
read_packet (OGGZ * oggz, oggz_packet * zp, long serialno, void * user_data)
{
  ogg_packet * op = &zp->op;

  if (newpage != 1) FAIL ("Packet is not the first on a new page");
  newpage = 0;

#ifdef DEBUG
  printf ("%08" PRI_OGGZ_OFF_T "x: serialno %010lu, "
	  "granulepos %" PRId64 ", packetno %" PRId64,
	  oggz_tell (oggz), serialno, op->granulepos, op->packetno);

  if (op->b_o_s) {
    printf (" *** bos");
  }

  if (op->e_o_s) {
    printf (" *** eos");
  }

  printf ("\n");
#endif

  if (op->bytes != 1)
    FAIL ("Packet too long");

  if (op->packet[0] != 'a' + read_iter/2)
    FAIL ("Packet contains incorrect data");

  if (op->granulepos != read_iter/2)
    FAIL ("Packet has incorrect granulepos");

  if (op->packetno != read_iter/2)
    FAIL ("Packet has incorrect packetno");

  read_iter++;
  if (read_iter >= 20) read_e_o_s = 1;

  return 0;
}

static size_t
my_io_write (void * user_handle, void * buf, size_t n)
{
  unsigned char * data_buf = (unsigned char *)user_handle;
  int len;

  /* Mark that the write IO method was actually used */
  write_called++;

  len = MIN ((int)n, DATA_BUF_LEN - write_offset);
  memcpy (&data_buf[write_offset], buf, len);

  write_offset += len;

  return len;
}

static int
test_flushing (int flush, char * filename)
{
  OGGZ * reader, * writer;
  unsigned char data_buf[DATA_BUF_LEN];
  long n;

  newpage = 0;

  hungry_iter = 0;
  hungry_e_o_s = 0;

  read_iter = 0;
  read_e_o_s = 0;

  write_called = 0;
  write_offset = 0;

  writer = oggz_new (OGGZ_WRITE);
  if (writer == NULL)
    FAIL("newly created OGGZ writer == NULL");

  serialno1 = oggz_serialno_new (writer);
  serialno2 = oggz_serialno_new (writer);

  if (oggz_write_set_hungry_callback (writer, hungry, 1, &flush) == -1)
    FAIL("Could not set hungry callback");

  oggz_io_set_write (writer, my_io_write, data_buf);


  reader = oggz_new (OGGZ_READ);
  if (reader == NULL)
    FAIL("newly created OGGZ reader == NULL");

  oggz_set_read_page (reader, -1, read_page, NULL);
  oggz_set_read_callback (reader, -1, read_packet, NULL);

  /* Write using the IO callback */
  n = oggz_write (writer, DATA_BUF_LEN);

  if (n == 0)
    FAIL("No data generated by writer");

  if (n >= DATA_BUF_LEN)
    FAIL("Too much data generated by writer");

  if (write_called == 0)
    FAIL("Write method ignored");

  /* Verify the contents */
  oggz_read_input (reader, data_buf, n);

  if (oggz_close (reader) != 0)
    FAIL("Could not close OGGZ reader");

  if (oggz_close (writer) != 0)
    FAIL("Could not close OGGZ writer");

  /* Optionally copy the generated stream to a file for manual checking */
  if (filename != NULL) {
    FILE * f;
    f = fopen (filename, "w");
    if (fwrite (data_buf, 1, n, f) < (size_t)n)
      FAIL("Unable to write generated stream to file");
    fclose (f);
  }

  return 0;
}

int
main (int argc, char * argv[])
{
  char * filename = NULL;

  INFO ("Testing page flushing");

  if (argc > 1) {
    filename = argv[1];
  }

  INFO ("+ OGGZ_FLUSH_BEFORE");
  test_flushing (OGGZ_FLUSH_BEFORE, filename);

  INFO ("+ OGGZ_FLUSH_AFTER");
  test_flushing (OGGZ_FLUSH_AFTER, filename);

  INFO ("+ OGGZ_FLUSH_BEFORE|OGGZ_FLUSH_AFTER");
  test_flushing (OGGZ_FLUSH_BEFORE|OGGZ_FLUSH_AFTER, filename);

  exit (0);
}
