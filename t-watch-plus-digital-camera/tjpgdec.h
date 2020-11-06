#ifndef _TJPGDEC_H_
#define _TJPGDEC_H_

#include <FS.h>
#include <rom/tjpgd.h>

#include "Arduino_GFX.h"

// Buffer is created during jpeg decode for sending data
// Total size of the buffer is  2 * (JPG_IMAGE_LINE_BUF_SIZE * 3)
// The size must be multiple of 256 bytes !!
#define JPG_IMAGE_LINE_BUF_SIZE 512
#define WORK_BUF_SIZE 3800 // Size of the working buffer (must be power of 2)

// 24-bit color type structure
typedef struct __attribute__((__packed__))
{
    //typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} color_t;

// ================ JPG SUPPORT ================================================
// User defined device identifier
typedef struct
{
    File f;             // File handler for input function
    int x;              // image top left point X position
    int y;              // image top left point Y position
    uint8_t *membuff;   // memory buffer containing the image
    uint32_t bufsize;   // size of the memory buffer
    uint32_t bufptr;    // memory buffer current position
    color_t *linbuf[2]; // memory buffer used for display output
    uint8_t linbuf_idx;
} JPGIODEV;

JPGIODEV dev;
char *work = 0; // Pointer to the working buffer (must be 4-byte aligned)

// User defined call-back function to input JPEG data from memory buffer
//-------------------------
static unsigned int tjd_buf_input(
    JDEC *jd,       // Decompression object
    BYTE *buff,     // Pointer to the read buffer (NULL:skip)
    unsigned int nd // Number of bytes to read/skip from input stream
)
{
    // Device identifier for the session (5th argument of jd_prepare function)
    JPGIODEV *dev = (JPGIODEV *)jd->device;
    if (!dev->membuff)
        return 0;
    if (dev->bufptr >= (dev->bufsize + 2))
        return 0; // end of stream

    if ((dev->bufptr + nd) > (dev->bufsize + 2))
        nd = (dev->bufsize + 2) - dev->bufptr;

    if (buff)
    { // Read nd bytes from the input strem
        memcpy(buff, dev->membuff + dev->bufptr, nd);
        dev->bufptr += nd;
        return nd; // Returns number of bytes read
    }
    else
    { // Remove nd bytes from the input stream
        dev->bufptr += nd;
        return nd;
    }
}

// User defined call-back function to input JPEG data from file
//---------------------
static unsigned int tjd_file_input(
    JDEC *jd,       // Decompression object
    BYTE *buff,     // Pointer to the read buffer (NULL:skip)
    unsigned int nd // Number of bytes to read/skip from input stream
)
{
    int rb = 0;
    // Device identifier for the session (5th argument of jd_prepare function)
    JPGIODEV *dev = (JPGIODEV *)jd->device;

    if (buff)
    { // Read nd bytes from the input strem
        rb = dev->f.read(buff, nd);
        return rb; // Returns actual number of bytes read
    }
    else
    { // Remove nd bytes from the input stream
        if (dev->f.seek(dev->f.position() + nd))
            return nd;
        else
            return 0;
    }
}

unsigned int tjd_output(
    JDEC *jd,     // Decompression object of current session
    void *bitmap, // Bitmap data to be output
    JRECT *rect   // Rectangular region to output
);

void decodeJpegBuff(uint8_t arrayname[], uint32_t array_size, uint8_t scale)
{
    JDEC jd; // Decompression object (70 bytes)
    JRESULT rc;

    //dev.fhndl = NULL;
    // image from buffer
    dev.membuff = arrayname;
    dev.bufsize = array_size;
    dev.bufptr = 0;

    if (scale > 3)
        scale = 3;

    if (work)
    {
        rc = jd_prepare(&jd, tjd_buf_input, (void *)work, WORK_BUF_SIZE, &dev);
        if (rc == JDR_OK)
        {
            // Start to decode the JPEG file
            rc = jd_decomp(&jd, tjd_output, scale);
        }
    }
}

void decodeJpegFile(FS fs, char filename[], uint8_t scale)
{
    JDEC jd; // Decompression object (70 bytes)
    JRESULT rc;

    dev.f = fs.open(filename);
    // image from buffer
    //dev.membuff = null;
    dev.bufsize = JPG_IMAGE_LINE_BUF_SIZE;
    dev.bufptr = 0;

    if (scale > 3)
        scale = 3;

    if (work)
    {
        rc = jd_prepare(&jd, tjd_file_input, (void *)work, WORK_BUF_SIZE, &dev);
        if (rc == JDR_OK)
        {
            // Start to decode the JPEG file
            rc = jd_decomp(&jd, tjd_output, scale);
        }
    }
}

#endif // _TJPGDEC_H_
