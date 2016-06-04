/* -*- c++ -*- */
/* 
 * Copyright 2015 <+YOU OR YOUR COMPANY+>.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "frame_decoder_cc_impl.h"

#include <cstdio>
#include <gnuradio/io_signature.h>
#include <pmt/pmt.h>

extern "C" {
#include <fec.h>
}

namespace gr {
  namespace ax100 {

    frame_decoder_cc::sptr
    frame_decoder_cc::make(bool verbose)
    {
      return gnuradio::get_initial_sptr
        (new frame_decoder_cc_impl(verbose));
    }

    /*
     * The private constructor
     */
    frame_decoder_cc_impl::frame_decoder_cc_impl(bool verbose)
      : gr::sync_block("frame_decoder_cc",
              gr::io_signature::make(1, 1, sizeof(unsigned char)),
              gr::io_signature::make(0, 0, 0))
    {
	    set_output_multiple(256*8);

	    message_port_register_out(pmt::mp("out"));

	    d_verbose = verbose;

    }

    /*
     * Our virtual destructor.
     */
    frame_decoder_cc_impl::~frame_decoder_cc_impl()
    {
	   delete d_pack;
    }

    int
    frame_decoder_cc_impl::work(int noutput_items,
        gr_vector_const_void_star &input_items,
        gr_vector_void_star &output_items)
    {
      const unsigned char *in = (const unsigned char *) input_items[0];
      unsigned char *out = (unsigned char *) output_items[0];
      unsigned char tmp;
      int rs_res;
      void *msg_data = NULL;
      unsigned char frame_data[255];
      unsigned char frame_len = 0;
      int i = 0;
      d_tags.clear();
      d_pack = new blocks::kernel::pack_k_bits(8);

      this->get_tags_in_window(d_tags, 0, 0, noutput_items);
      for(d_tags_itr = d_tags.begin(); d_tags_itr != d_tags.end(); d_tags_itr++) {
		// Check that we have enough data for a full frame
		if ((d_tags_itr->offset - this->nitems_read(0)) > (noutput_items - 255 * 8)) {
			//printf("Not enough samples for full packet at tag offset: %lu\r\n", d_tags_itr->offset - this->nitems_read(0));
			return (d_tags_itr->offset - this->nitems_read(0) - 1);
		}

		d_pack->pack(frame_data, &in[d_tags_itr->offset - this->nitems_read(0)], 1);
		d_pack->pack(&frame_data[1], &in[d_tags_itr->offset + 8 - this->nitems_read(0)], frame_data[0] - 1);
		rs_res = decode_rs_8(&frame_data[1], NULL, 0, 255 - frame_data[0] + 1);

		// Send via GNUradio message if RS ok
		if (rs_res >= 0) {
			// Swap CSP header
			tmp = frame_data[4];
			frame_data[4] = frame_data[1];
			frame_data[1] = tmp;
			tmp = frame_data[3];
			frame_data[3] = frame_data[2];
			frame_data[2] = tmp;

			// len_byte + csp_packet - rs = frame_len - 32
			frame_data[0] -= 32;
			frame_len = frame_data[0];
			// Get csp dest
			frame_data[0] = ((frame_data[4] & 1) << 4) + ((frame_data[3] >> 4) & 0x0f);
		        // Send by GNUradio message
			message_port_pub(pmt::mp("out"),
					 pmt::cons(pmt::PMT_NIL,
						   pmt::init_u8vector(frame_len-1,
								      frame_data+1)));
		}
		if (d_verbose) {
		  std::printf("\r\n------------ FRAME INFO -------------\r\n");
		  std::printf("Length: %d\r\nBytes corrected: %d\r\n", frame_len, rs_res);
		  std::printf("Dest: %d\r\n", frame_data[0]);
		}

      }

      // Tell runtime system how many output items we produced.
      return noutput_items;
    }


  } /* namespace ax100 */
} /* namespace gr */

