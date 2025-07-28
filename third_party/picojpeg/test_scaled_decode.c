//------------------------------------------------------------------------------
// test_scaled_decode.c
// Test program for the new scaled JPEG decoding functionality
//------------------------------------------------------------------------------
#include "picojpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *g_pInFile;
static uint g_nInFileSize;
static uint g_nInFileOfs;

typedef unsigned int uint;

//------------------------------------------------------------------------------
unsigned char pjpeg_need_bytes_callback(unsigned char* pBuf, unsigned char buf_size, unsigned char *pBytes_actually_read, void *pCallback_data)
{
   uint n;
   (void)pCallback_data;
   
   n = (g_nInFileSize - g_nInFileOfs < buf_size) ? (g_nInFileSize - g_nInFileOfs) : buf_size;
   if (n && (fread(pBuf, 1, n, g_pInFile) != n))
      return PJPG_STREAM_READ_ERROR;
   *pBytes_actually_read = (unsigned char)(n);
   g_nInFileOfs += n;
   return 0;
}

//------------------------------------------------------------------------------
void write_ppm(const char* filename, unsigned char* pImage, int width, int height, int comps)
{
   FILE* pFile = fopen(filename, "wb");
   if (!pFile) return;
   
   if (comps == 1)
   {
      fprintf(pFile, "P5\n%d %d\n255\n", width, height);
      fwrite(pImage, 1, width * height, pFile);
   }
   else
   {
      fprintf(pFile, "P6\n%d %d\n255\n", width, height);
      fwrite(pImage, 1, width * height * 3, pFile);
   }
   
   fclose(pFile);
}

//------------------------------------------------------------------------------
int test_decode(const char* filename, unsigned char scale_factor)
{
   pjpeg_image_info_t image_info;
   unsigned char status;
   unsigned char* pImage;
   int row_blocks_remaining, mcu_x, mcu_y;
   int block_size_pixels;
   
   printf("Testing scale factor %d (1/%d size)\n", scale_factor, scale_factor);
   
   g_pInFile = fopen(filename, "rb");
   if (!g_pInFile)
   {
      printf("Failed to open file: %s\n", filename);
      return -1;
   }
   
   fseek(g_pInFile, 0, SEEK_END);
   g_nInFileSize = ftell(g_pInFile);
   fseek(g_pInFile, 0, SEEK_SET);
   g_nInFileOfs = 0;
   
   // Use new scaled API
   status = pjpeg_decode_init_scale(&image_info, pjpeg_need_bytes_callback, NULL, scale_factor);
   if (status)
   {
      printf("pjpeg_decode_init_scale() failed with status %d\n", status);
      fclose(g_pInFile);
      return -1;
   }
   
   printf("Image info: %dx%d, %d comps, scan type: %d\n", 
          image_info.m_width, image_info.m_height, image_info.m_comps, image_info.m_scanType);
   printf("MCUs: %dx%d, MCU size: %dx%d\n", 
          image_info.m_MCUSPerRow, image_info.m_MCUSPerCol, 
          image_info.m_MCUWidth, image_info.m_MCUHeight);
   
   // Determine effective block size based on scale
   block_size_pixels = 8 / scale_factor;
   if (block_size_pixels < 1) block_size_pixels = 1;
   
   printf("Block size: %dx%d pixels\n", block_size_pixels, block_size_pixels);
   
   // Allocate output image buffer
   int output_width = image_info.m_MCUSPerRow * block_size_pixels;
   int output_height = image_info.m_MCUSPerCol * block_size_pixels;
   pImage = malloc(output_width * output_height * 3); // Always allocate for RGB
   if (!pImage)
   {
      printf("Failed to allocate image buffer\n");
      fclose(g_pInFile);
      return -1;
   }
   memset(pImage, 0, output_width * output_height * 3);
   
   printf("Output image size: %dx%d\n", output_width, output_height);
   
   // Decode MCUs
   row_blocks_remaining = image_info.m_MCUSPerCol;
   mcu_y = 0;
   
   while (row_blocks_remaining)
   {
      for (mcu_x = 0; mcu_x < image_info.m_MCUSPerRow; mcu_x++)
      {
         status = pjpeg_decode_mcu();
         if (status)
         {
            if (status == PJPG_NO_MORE_BLOCKS)
               break;
            printf("pjpeg_decode_mcu() failed with status %d\n", status);
            free(pImage);
            fclose(g_pInFile);
            return -1;
         }
         
         // Copy MCU data to output image
         int dst_x = mcu_x * block_size_pixels;
         int dst_y = mcu_y * block_size_pixels;
         
         for (int by = 0; by < block_size_pixels; by++)
         {
            for (int bx = 0; bx < block_size_pixels; bx++)
            {
               int src_idx = by * 8 + bx; // Source is always 8-pixel stride
               int dst_idx = ((dst_y + by) * output_width + (dst_x + bx)) * 3;
               
               if (image_info.m_comps == 1)
               {
                  // Grayscale
                  unsigned char y = image_info.m_pMCUBufR[src_idx];
                  pImage[dst_idx + 0] = y;
                  pImage[dst_idx + 1] = y;
                  pImage[dst_idx + 2] = y;
               }
               else
               {
                  // Color
                  pImage[dst_idx + 0] = image_info.m_pMCUBufR[src_idx];
                  pImage[dst_idx + 1] = image_info.m_pMCUBufG[src_idx];
                  pImage[dst_idx + 2] = image_info.m_pMCUBufB[src_idx];
               }
            }
         }
      }
      
      mcu_y++;
      row_blocks_remaining--;
   }
   
   // Write output file
   char output_filename[256];
   snprintf(output_filename, sizeof(output_filename), "test_scale_%d.ppm", scale_factor);
   write_ppm(output_filename, pImage, output_width, output_height, 3);
   printf("Wrote output: %s\n", output_filename);
   
   free(pImage);
   fclose(g_pInFile);
   return 0;
}

//------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
   if (argc != 2)
   {
      printf("Usage: %s <input.jpg>\n", argv[0]);
      return 1;
   }
   
   printf("Testing scaled JPEG decoding with picojpeg\n");
   printf("Input file: %s\n", argv[1]);
   
   // Test different scale factors
   unsigned char scales[] = {1, 2, 4, 8};
   int num_scales = sizeof(scales) / sizeof(scales[0]);
   
   for (int i = 0; i < num_scales; i++)
   {
      printf("\n--- Scale Factor %d ---\n", scales[i]);
      if (test_decode(argv[1], scales[i]) != 0)
      {
         printf("Test failed for scale factor %d\n", scales[i]);
         return 1;
      }
   }
   
   printf("\nAll tests completed successfully!\n");
   return 0;
}