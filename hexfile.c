/**
   \file hexfile.c
   
   \author G. Icking-Konert
   \date 2018-12-14
   \version 0.2
   
   \brief implementation of routines for HEX, S19 and table files
   
   implementation of routines for importing and exporting Motorola S19 and Intel HEX files, 
   as well as plain ASCII tables.  
   (format descriptions under http://en.wikipedia.org/wiki/SREC_(file_format) or
   http://www.keil.com/support/docs/1584.htm). 
*/

// include files
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>
#include "hexfile.h"
#include "main.h"
#include "misc.h"


/**
   \fn char *get_line(char **buf, char *line)
   
   \param[in]  buf        pointer to read from (is updated)
   \param[out] line       pointer to line read (has to be large anough)
   
   read line (until LF, CR, or EOF) from RAM buffer and advance buffer pointer.
   memory for line has to be allocated externally
*/
char *get_line(char **buf, char *line) {
  
  char  *p = line;
  
  // copy line
  while ((**buf!=10) && (**buf!=13) && (**buf!=0)) {
    *line = **buf;
    line++;
    (*buf)++;
  }
  
  // skip CR + LF in buffer
  while ((**buf==10) || (**buf==13))
    (*buf)++;
    
  // terminate line
  *line = '\0';
  
  // check if data was copied
  if (p == line)
    return(NULL);
  else
    return(p);
  
} // get_line

  

/**
   \fn void load_file(const char *filename, char *fileBuf, uint32_t *lenFileBuf, uint8_t verbose)
   
   \param[in]  filename     name of file to read
   \param[out] fileBuf      memory buffer containing file content
   \param[out] lenFileBuf   size of data [B] read from file
   \param[in]  verbose      verbosity level (0=MUTE, 1=SILENT, 2=INFORM, 3=CHATTY)
   
   read file from file to memory buffer. Don't interpret (is done in separate routine)
*/
void load_file(const char *filename, char *fileBuf, uint32_t *lenFileBuf, uint8_t verbose) {

  FILE      *fp;
  
  // strip path from filename for readability
  #if defined(WIN32)
    const char *shortname = strrchr(filename, '\\');
  #else
    const char *shortname = strrchr(filename, '/');
  #endif
  if (!shortname)
    shortname = filename;
  else
    shortname++;

  // print message
  if (verbose >= SILENT)
    printf("  load '%s' ... ", shortname);
  fflush(stdout);

  // open file to read
  if (!(fp = fopen(filename, "rb")))
    Error("Failed to open file %s", filename);
     
  // get filesize
  fseek(fp, 0, SEEK_END);
  (*lenFileBuf) = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  // check file size vs. buffer
  if ((*lenFileBuf) > LENFILEBUF)
    Error("File %s exceeded buffer size (%d vs %d)", (*lenFileBuf), LENFILEBUF);

  // init memory image to zero
  memset(fileBuf, 0, LENFILEBUF * sizeof(*fileBuf));

  // read file to buffer
  fread(fileBuf, (*lenFileBuf), 1, fp);
  
  // close file again
  fclose(fp);

  // print message
  if ((verbose == SILENT) || (verbose == INFORM)){
    printf("done\n");
  }
  else if (verbose == CHATTY) {
    if ((*lenFileBuf)>2048)
      printf("done (%1.1fkB)\n", (float) (*lenFileBuf)/1024.0);
    else if ((*lenFileBuf)>0)
      printf("done (%dB)\n", (*lenFileBuf));
    else
      printf("done, no data read\n");
  }
  fflush(stdout);

} // load_file



/**
   \fn void convert_s19(char *fileBuf, uint32_t lenFileBuf, uint16_t *imageBuf, uint8_t verbose)
   
   \param[in]  fileBuf      memory buffer to read from
   \param[in]  lenFileBuf   length of memory buffer
   \param[out] imageBuf     RAM image of file. HB!=0 indicates content
   \param[in]  verbose      verbosity level (0=MUTE, 1=SILENT, 2=INFORM, 3=CHATTY)
   
   convert memory buffer containing s19 hexfile to memory image. For description of 
   Motorola S19 file format see http://en.wikipedia.org/wiki/SREC_(file_format)
*/
void convert_s19(char *fileBuf, uint32_t lenFileBuf, uint16_t *imageBuf, uint8_t verbose) {
  
  char      line[1000], tmp[1000], *p;
  int       linecount, idx;
  uint8_t   type, len, chkRead, chkCalc;
  uint32_t  addr, addrStart, addrStop;
  uint32_t  val, numData;
  
  // print message
  if (verbose == INFORM)
    printf("  convert S19 ... ");
  else if (verbose == CHATTY)
    printf("  convert Motorola S19 file ... ");
  fflush(stdout);


  //////
  // import data to memory with syntax check
  //////
  p = fileBuf;
  linecount = 0;
  numData = 0;
  addrStart = 0xFFFFFFFF;
  addrStop  = 0x00000000;
  while ((uint32_t) (p-fileBuf) < lenFileBuf) {
  
    // get next line. On EOF terminate
    if (!get_line(&p, line))
      break;

    // increase line counter
    linecount++;
    chkCalc = 0x00;
    
    // check 1st char (must be 'S')
    if (line[0] != 'S')
      Error("Line %d in Motorola S-record file: line does not start with 'S'", linecount);
    
    // record type
    type = line[1]-48;
    
    // skip if line contains no data
    if ((type==0) || (type==8) || (type==9))
      continue; 
    
    // record length (address + data + checksum)
    sprintf(tmp,"0x00");
    strncpy(tmp+2, line+2, 2);
    sscanf(tmp, "%x", &val);
    len = val;
    chkCalc += val;              // increase checksum
    
    // address (S1=16bit, S2=24bit, S3=32bit)
    addr = 0;
    for (int i=0; i<type+1; i++) {
      sprintf(tmp,"0x00");
      tmp[2] = line[4+(i*2)];
      tmp[3] = line[5+(i*2)];
      sscanf(tmp, "%x", &val);
      addr *= 256;
      addr += val;    
      chkCalc += val;
    } 
    
    // check for buffer overflow
    if (addr > LENIMAGEBUF-1)
      Error("Line %d in Motorola S-record file: buffer size exceeded (%dMB vs %dMB)", linecount, (int) (addr/1024L/1024L), (int) (LENIMAGEBUF/1024L/1024L));

    // read record data
    idx=6+(type*2);                     // start at position 8, 10, or 12, depending on record type
    len=len-1-(1+type);                 // substract chk and address length
    for (int i=0; i<len; i++) {
      sprintf(tmp,"0x00");
      strncpy(tmp+2, line+idx, 2);      // get next 2 chars as string
      sscanf(tmp, "%x", &val);          // interpret as hex data
      imageBuf[addr+i] = val | 0xFF00;  // store data byte in buffer and set high byte for "defined"
      numData++;                        // increade byte counter
      chkCalc += val;                   // increase checksum
      idx+=2;                           // advance 2 chars in line
    }
       
    // for printout store min/max address in file
    if (addr       < addrStart)  addrStart = addr;
    if (addr+len-1 > addrStop)   addrStop  = addr+len-1;

    // checksum
    sprintf(tmp,"0x00");
    strncpy(tmp+2, line+idx, 2);
    sscanf(tmp, "%x", &val);
    chkRead = val;

    // assert checksum (0xFF xor (sum over all except record type)
    chkCalc ^= 0xFF;                 // invert checksum
    if (chkCalc != chkRead)
      Error("Line %d in Motorola S-record file: checksum error (0x%02x vs. 0x%02x)", linecount, chkRead, chkCalc);
    
  } // while !EOF

  // print message
  if (verbose == INFORM) {
    printf("done\n");
  }
  else if (verbose == CHATTY) {
    if (numData>2048)
      printf("done (%1.1fkB in 0x%04x - 0x%04x)\n", (float) numData/1024.0, addrStart, addrStop);
    else if (numData>0)
      printf("done (%dB in 0x%04x - 0x%04x)\n", numData, addrStart, addrStop);
    else
      printf("done, no data\n");
  }
  fflush(stdout);
  
} // convert_s19

  

/**
   \fn void convert_ihx(char *fileBuf, uint32_t lenFileBuf, uint16_t *imageBuf, uint8_t verbose)
   
   \param[in]  fileBuf      memory buffer to read from
   \param[in]  lenFileBuf   length of memory buffer
   \param[out] imageBuf     RAM image of file. HB!=0 indicates content
   \param[in]  verbose      verbosity level (0=MUTE, 1=SILENT, 2=INFORM, 3=CHATTY)

   convert memory buffer containing intel hexfile to memory buffer. For description of 
   Intel hex file format see http://en.wikipedia.org/wiki/Intel_HEX
*/
void convert_ihx(char *fileBuf, uint32_t lenFileBuf, uint16_t *imageBuf, uint8_t verbose) {
  
  char      line[1000], tmp[1000], *p;
  int       linecount, idx;
  uint8_t   type, len, chkRead, chkCalc;
  uint32_t  addr, addrStart, addrStop;
  uint32_t  val, numData;
  uint32_t  addrOff, addrJumpStart;

  // avoid compiler warning (variable not yet used). See https://stackoverflow.com/questions/3599160/unused-parameter-warnings-in-c
  (void) (addrJumpStart);

  // print message
  if (verbose == INFORM)
    printf("  convert IHX ... ");
  else if (verbose == CHATTY)
    printf("  convert Intel HEX file ... ");
  fflush(stdout);


  //////
  // import data to memory with syntax check
  //////
  p = fileBuf;
  linecount = 0;
  numData = 0;
  addrStart = 0xFFFFFFFF;
  addrStop  = 0x00000000;
  addrOff = 0x00000000;
  while ((uint32_t) (p-fileBuf) < lenFileBuf) {
  
    // get next line. On EOF terminate
    if (!get_line(&p, line))
      break;

    // increase line counter
    linecount++;
    chkCalc = 0x00;
    
    // check 1st char (must be ':')
    if (line[0] != ':')
      Error("Line %d in Intel hex file: line does not start with ':'", linecount);
    
    // record length (address + data + checksum)
    sprintf(tmp,"0x00");
    strncpy(tmp+2, line+1, 2);
    sscanf(tmp, "%x", &val);
    len = val;
    chkCalc += len;              // increase checksum
    
    // 16b address
    addr = 0;
    sprintf(tmp,"0x0000");
    strncpy(tmp+2, line+3, 4);
    sscanf(tmp, "%x", &val);
    chkCalc += (uint8_t) (val >> 8);
    chkCalc += (uint8_t)  val;
    addr = val + addrOff;         // add offset for >64kB addresses

    // record type
    sprintf(tmp,"0x00");
    strncpy(tmp+2, line+7, 2);
    sscanf(tmp, "%x", &val);
    type = val;
    chkCalc += type;              // increase checksum
    
    // record contains data
    if (type==0) {
      
      // check for buffer overflow
      if (addr > LENIMAGEBUF-1)
        Error("Line %d in Intel hex file: buffer size exceeded (%dMB vs %dMB)", linecount, (int) (addr/1024L/1024L), (int) (LENIMAGEBUF/1024L/1024L));

      // for printout store min/max address in file
      if (addr < addrStart)  addrStart = addr;
      if (addr > addrStop)   addrStop  = addr;
    
      // get data
      idx = 9;                            // start at index 9
      for (int i=0; i<len; i++) {
        sprintf(tmp,"0x00");
        strncpy(tmp+2, line+idx, 2);      // get next 2 chars as string
        sscanf(tmp, "%x", &val);          // interpret as hex data
        imageBuf[addr+i] = val | 0xFF00;  // store data byte in buffer and set high byte for "defined"
        numData++;                        // increade byte counter
        chkCalc += val;                   // increase checksum
        idx+=2;                           // advance 2 chars in line
      }
      
    } // type==0

    // EOF indicator
    else if (type==1)
      continue; 

    // extended segment addresses not yet supported
    else if (type==2) 
      Error("Line %d in Intel hex file: extended segment address type 2 not supported", linecount);
    
    // start segment address (only relevant for 80x86 processors, ignore here) 
    else if (type==3)
      continue;
    
    // extended address (=upper 16b of address for following data records)
    else if (type==4) {
      idx = 13;                       // start at index 13
      sprintf(tmp,"0x0000");
      strncpy(tmp+2, line+9, 4);      // get next 4 chars as string
      sscanf(tmp, "%x", &val);        // interpret as hex data
      chkCalc += (uint8_t) (val >> 8);
      chkCalc += (uint8_t)  val;
      addrOff = val << 16;
    } // type==4
    
    // start linear address records. Can be ignored, see http://www.keil.com/support/docs/1584/
    else if (type==5)
      continue; 
    
    // unsupported record type -> error
    else
      Error("Line %d in Intel hex file: unsupported type %d", linecount, type);
    
    
    // checksum
    sprintf(tmp,"0x00");
    strncpy(tmp+2, line+idx, 2);
    sscanf(tmp, "%x", &val);
    chkRead = val;
    
    // assert checksum (0xFF xor (sum over all except record type))
    chkCalc = 255 - chkCalc + 1;                 // calculate 2-complement
    if (chkCalc != chkRead)
      Error("Line %d in Intel hex file: checksum error (read 0x%02x, calc 0x%02x)", linecount, chkRead, chkCalc);
    
  } // while !EOF
  
  // print message
  if (verbose == INFORM) {
    printf("done\n");
  }
  else if (verbose == CHATTY) {
    if (numData>2048)
      printf("done (%1.1fkB in 0x%04x - 0x%04x)\n", (float) numData/1024.0, addrStart, addrStop);
    else if (numData>0)
      printf("done (%dB in 0x%04x - 0x%04x)\n", numData, addrStart, addrStop);
    else
      printf("done, no data\n");
  }
  fflush(stdout);
  
} // convert_ihx

  

/**
   \fn void convert_txt(char *fileBuf, uint32_t lenFileBuf, uint16_t *imageBuf, uint8_t verbose)
   
   \param[in]  fileBuf      memory buffer to read from
   \param[in]  lenFileBuf   length of memory buffer
   \param[out] imageBuf     RAM image of file. HB!=0 indicates content
   \param[in]  verbose      verbosity level (0=MUTE, 1=SILENT, 2=INFORM, 3=CHATTY)

   convert memory buffer containing plain table (address / value) to memory buffer. 
   Address and value may be decimal (plain numberst) or hexadecimal (starting with '0x').
   Lines starting with '#' are ignored. No syntax check is performed.
*/
void convert_txt(char *fileBuf, uint32_t lenFileBuf, uint16_t *imageBuf, uint8_t verbose) {
  
  char      line[1000], *p;
  int       linecount;
  char      sAddr[1000], sValue[1000];
  uint32_t  addr, addrStart, addrStop;
  uint32_t  val, numData;
  
  // print message
  if (verbose == INFORM)
    printf("  convert table ... ");
  else if (verbose == CHATTY)
    printf("  convert ASCII table file ... ");
  fflush(stdout);


  //////
  // import data to memory with syntax check
  //////
  p = fileBuf;
  linecount = 0;
  numData = 0;
  addrStart = 0xFFFFFFFF;
  addrStop  = 0x00000000;
  while ((uint32_t) (p-fileBuf) < lenFileBuf) {
  
    // get next line. On EOF terminate
    if (!get_line(&p, line))
      break;

    // increase line counter
    linecount++;
    
    // if line starts with '#' ignore as comment
    if (line[0] == '#')
      continue;

    // get address and value as string 
    sscanf(line, "%s %s", sAddr, sValue);
    
    
    //////////
    // extract address
    //////////

    // address string is in hex format (starts with '0x')
    if ((sAddr[0] == '0') && ((sAddr[1] == 'x') || (sAddr[1] == 'X'))) {

      // check for valid characters 0-9, A-F
      for (int i=2; i<strlen(sAddr); i++) {
        if (!isxdigit(sAddr[i]))
          Error("Line %d in table file: hex address '%s' contains invalid character ('%c')", linecount, sAddr, sAddr[i]);
      }

      // get address
      sscanf(sAddr, "%x", &addr);

    } // address is in hex format
    
    // address string is in decimal format
    else {

      // check for valid characters 0-9
      for (int i=0; i<strlen(sAddr); i++) {
        if (!isdigit(sAddr[i]))
          Error("Line %d in table file: dec address '%s' contains invalid character ('%c')", linecount, sAddr, sAddr[i]);
      }

      // get address
      sscanf(sAddr, "%d", &addr);

    } // extract address
    
    
    //////////
    // extract value
    //////////
    
    // value string is in hex format (starts with '0x')
    if ((sValue[0] == '0') && ((sValue[1] == 'x') || (sValue[1] == 'X'))) {

      // check for valid characters 0-9, A-F
      for (int i=2; i<strlen(sValue); i++) {
        if (!isxdigit(sValue[i]))
          Error("Line %d in table file: hex value '%s' contains invalid character ('%c')", linecount, sValue, sValue[i]);
      }

      // get address
      sscanf(sValue, "%x", &val);

    } // address is in hex format
    
    // address string is in decimal format
    else {

      // check for valid characters 0-9
      for (int i=0; i<strlen(sValue); i++) {
        if (!isdigit(sValue[i]))
          Error("Line %d in table file: dec value '%s' contains invalid character ('%c')", linecount, sValue, sValue[i]);
      }

      // get address
      sscanf(sValue, "%d", &val);

    } // extract value
    
    // check for buffer overflow
    if (addr > LENIMAGEBUF-1)
      Error("Line %d in table file: buffer size exceeded (%dMB vs %dMB)", linecount, (int) (addr/1024L/1024L), (int) (LENIMAGEBUF/1024L/1024L));
       
    // for printout store min/max address in file
    if (addr < addrStart)  addrStart = addr;
    if (addr > addrStop)   addrStop  = addr;

    // store data byte in buffer and set high byte
    imageBuf[addr] = (uint16_t) val | 0xFF00;   
    numData++; 
    
  } // while !EOF
 
  // print message
  if (verbose == INFORM) {
    printf("done\n");
  }
  else if (verbose == CHATTY) {
    if (numData>2048)
      printf("done (%1.1fkB in 0x%04x - 0x%04x)\n", (float) numData/1024.0, addrStart, addrStop);
    else if (numData>0)
      printf("done (%dB in 0x%04x - 0x%04x)\n", numData, addrStart, addrStop);
    else
      printf("done, no data\n");
  }
  fflush(stdout);
 
} // convert_txt

  

/**
   \fn convert_bin(char *fileBuf, uint32_t lenFileBuf, uint32_t addrStart, uint16_t *imageBuf, uint8_t verbose)
   
   \param[in]  fileBuf      memory buffer to read from
   \param[in]  lenFileBuf   length of memory buffer
   \param[in]  addrStart    address offset for binary import
   \param[out] imageBuf     RAM image of file. HB!=0 indicates content
   \param[in]  verbose      verbosity level (0=MUTE, 1=SILENT, 2=INFORM, 3=CHATTY)

   convert memory buffer containing binary data to memory image. Binary data contains no absolute addresses,
   just data. Therefor a starting address must also be provided.
*/
void convert_bin(char *fileBuf, uint32_t lenFileBuf, uint32_t addrStart, uint16_t *imageBuf, uint8_t verbose) {
  
  uint32_t  addrStop, numData;
  
  // print message
  if (verbose == INFORM)
    printf("  convert binary ... ");
  else if (verbose == CHATTY)
    printf("  convert binary data ... ");
  fflush(stdout);

  // calculate number of bytes and last address
  numData  = lenFileBuf;
  addrStop = addrStart + numData;

  // check for buffer overflow
  if (addrStop > LENIMAGEBUF-1)
    Error("Binary file conversion: buffer size exceeded (%dMB vs %dMB)", (int) (addrStop/1024L/1024L), (int) (LENIMAGEBUF/1024L/1024L));

  // copy data and mark as set (HB=0xFF)
  for (int i=0; i<numData; i++) {
    imageBuf[addrStart+i] = ((uint16_t) fileBuf[i]) | 0xFF00;
  }

  // print message
  if (verbose == INFORM) {
    printf("done\n");
  }
  else if (verbose == CHATTY) {
    if (numData>2048)
      printf("done (%1.1fkB in 0x%04x - 0x%04x)\n", (float) numData/1024.0, addrStart, addrStop);
    else if (numData>0)
      printf("done (%dB in 0x%04x - 0x%04x)\n", numData, addrStart, addrStop);
    else
      printf("done, no data\n");
  }
  fflush(stdout);

} // convert_bin

  

/**
   \fn get_image_size(uint16_t *imageBuf, uint32_t scanStart, uint32_t scanStop, uint32_t *addrStart, uint32_t *addrStop, uint32_t *numData)
   
   \param[in]  imageBuf     memory image containing data. HB!=0 indicates content
   \param[in]  scanStart    start address for scan
   \param[in]  scanStop     end address for scan
   \param[out] addrStop     last address containing data (HB!=0x00)
   \param[out] addrStop     last address containing data (HB!=0x00)
   \param[out] numData      number of data bytes in image (HB!=0x00)
   \param[in]  verbose      verbosity level (0=MUTE, 1=SILENT, 2=INFORM, 3=CHATTY)

   Get fist and last address and number of bytes in memory image. Defined data is indicated by HB!=0x00
*/
void get_image_size(uint16_t *imageBuf, uint32_t scanStart, uint32_t scanStop, uint32_t *addrStart, uint32_t *addrStop, uint32_t *numData) {
  
  // simple checks of scan window
  if (scanStart > scanStop)
    Error("scan start address 0x%04x higher than end address 0x%04x", scanStart, scanStop);
  if (scanStart > LENIMAGEBUF)
    Error("scan start address 0x%04x exceeds buffer size 0x%04x", scanStart, LENIMAGEBUF);
  if (scanStop > LENIMAGEBUF)
    Error("scan end address 0x%04x exceeds buffer size 0x%04x", scanStop, LENIMAGEBUF);

  // loop though image and check for defined data (HB!=0x00)
  *addrStart = 0xFFFFFFFF;
  *addrStop  = 0x00000000;
  *numData   = 0;
  for (uint32_t addr=scanStart; addr<=scanStop; addr++) {
    
    // entry contains data (HB!=0x00) 
    if (imageBuf[addr] & 0xFF00) {
      if (addr < *addrStart) *addrStart = addr;
      if (addr > *addrStop)  *addrStop  = addr;
      (*numData)++;
    }

  } // loop over image

} // get_image_size

  

/**
   \fn clip_image(uint16_t *imageBuf, uint32_t addrStart, uint32_t addrStop, uint8_t verbose)
   
   \param      imageBuf     memory image containing data. HB!=0 indicates content
   \param[in]  addrStart    starting address of clipping window
   \param[in]  addrStop     topmost address of clipping window
   \param[in]  verbose      verbosity level (0=MUTE, 1=SILENT, 2=INFORM, 3=CHATTY)

   Clip memory image to specified window, i.e. reset all data outside specified window to "undefined" (HB=0x00)
*/
void clip_image(uint16_t *imageBuf, uint32_t addrStart, uint32_t addrStop, uint8_t verbose) {
  
  uint32_t  numCleared;

  // print message
  if (verbose == INFORM)
    printf("  clip image ... ");
  else if (verbose == CHATTY)
    printf("  clip memory image ... ");
  fflush(stdout);
  
  // simple checks of scan window
  if (addrStart > addrStop)
    Error("start address 0x%04x higher than end address 0x%04x", addrStart, addrStop);
  if (addrStart > LENIMAGEBUF)
    Error("start address 0x%04x exceeds buffer size 0x%04x", addrStart, LENIMAGEBUF);
  if (addrStop > LENIMAGEBUF)
    Error("end address 0x%04x exceeds buffer size 0x%04x", addrStop, LENIMAGEBUF);

  // loop over memory image and clear all data outside specified clipping window
  numCleared = 0;
  for (uint32_t addr=0; addr<LENIMAGEBUF; addr++) {
    if ((addr < addrStart) || (addr > addrStop)) {
      if (imageBuf[addr] & 0xFF00)
         numCleared++;                 // count deleted bytes for output below
      imageBuf[addr] = 0x0000;         // HB=0x00 indicates data undefined, LB contains data
    }
  }

  // print message
  if (verbose == INFORM) {
    printf("done\n");
  }
  else if (verbose == CHATTY) {
    if (numCleared>2048)
      printf("done, clipped %1.1fkB outside 0x%04x - 0x%04x\n", (float) numCleared/1024.0, addrStart, addrStop);
    else if (numCleared>0)
      printf("done, clipped %dB outside 0x%04x - 0x%04x\n", numCleared, addrStart, addrStop);
    else
      printf("done, no data cleared\n");
  }
  fflush(stdout);
 
} // clip_image

  

/**
   \fn clear_image(uint16_t *imageBuf, uint32_t addrStart, uint32_t addrStop, uint8_t verbose)
   
   \param      imageBuf     memory image containing data. HB!=0 indicates content
   \param[in]  addrStart    starting address of section to clear
   \param[in]  addrStop     topmost address of section to clear
   \param[in]  verbose      verbosity level (0=MUTE, 1=SILENT, 2=INFORM, 3=CHATTY)

   Clear data in memory image to "undefined", i.e. reset all data inside specified window to "undefined" (HB=0x00)
*/
void clear_image(uint16_t *imageBuf, uint32_t addrStart, uint32_t addrStop, uint8_t verbose) {
  
  uint32_t  numCleared;

  // print message
  if (verbose == INFORM)
    printf("  clear image ... ");
  else if (verbose == CHATTY)
    printf("  clear memory image ... ");
  fflush(stdout);
  
  // simple checks of scan window
  if (addrStart > addrStop)
    Error("start address 0x%04x higher than end address 0x%04x", addrStart, addrStop);
  if (addrStart > LENIMAGEBUF)
    Error("start address 0x%04x exceeds buffer size 0x%04x", addrStart, LENIMAGEBUF);
  if (addrStop > LENIMAGEBUF)
    Error("end address 0x%04x exceeds buffer size 0x%04x", addrStop, LENIMAGEBUF);

  // loop over memory image and clear all data inside specified window
  numCleared = 0;
  for (uint32_t addr=0; addr<LENIMAGEBUF; addr++) {
    if ((addr >= addrStart) && (addr <= addrStop)) {
      if (imageBuf[addr] & 0xFF00)
         numCleared++;                 // count deleted bytes for output below
      imageBuf[addr] = 0x0000;         // HB=0x00 indicates data undefined, LB contains data
    }
  }

  // print message
  if (verbose == INFORM) {
    printf("done\n");
  }
  else if (verbose == CHATTY) {
    if (numCleared>2048)
      printf("done, cleared %1.1fkB in 0x%04x - 0x%04x\n", (float) numCleared/1024.0, addrStart, addrStop);
    else if (numCleared>0)
      printf("done, cleared %dB in 0x%04x - 0x%04x\n", numCleared, addrStart, addrStop);
    else
      printf("done, no data cleared\n");
  }
  fflush(stdout);
 
} // clear_image

  

/**
   \fn copy_image(uint16_t *imageBuf, uint32_t sourceStart, uint32_t sourceStop, uint32_t targetStart, uint8_t verbose)
   
   \param      imageBuf     memory image containing data. HB!=0 indicates content
   \param[in]  sourceStart  starting address to copy from
   \param[in]  sourceStart  last address to copy from
   \param[in]  targetStart  starting address to copy to
   \param[in]  verbose      verbosity level (0=MUTE, 1=SILENT, 2=INFORM, 3=CHATTY)

   Copy data section within image to new address. Data at old address is maintained (if sections don't overlap).
*/
void copy_image(uint16_t *imageBuf, uint32_t sourceStart, uint32_t sourceStop, uint32_t targetStart, uint8_t verbose) {
  
  // print message
  if (verbose == INFORM)
    printf("  copy data ... ");
  else if (verbose == CHATTY)
    printf("  copy image data ... ");
  fflush(stdout);
  
  // simple checks of scan window
  if (sourceStart > sourceStop)
    Error("source start address 0x%04x higher than end address 0x%04x", sourceStart, sourceStop);
  if (sourceStart > LENIMAGEBUF)
    Error("source start address 0x%04x exceeds buffer size 0x%04x", sourceStart, LENIMAGEBUF);
  if (sourceStop > LENIMAGEBUF)
    Error("source end address 0x%04x exceeds buffer size 0x%04x", sourceStop, LENIMAGEBUF);
  if (targetStart > LENIMAGEBUF)
    Error("target start address 0x%04x exceeds buffer size 0x%04x", targetStart, LENIMAGEBUF);
  if (targetStart+(sourceStop-sourceStart+1) > LENIMAGEBUF)
    Error("target end address 0x%04x exceeds buffer size 0x%04x", targetStart+(sourceStop-sourceStart+1), LENIMAGEBUF);

  // get number of data to copy (HB!=0x00)
  int numCopied = 0;
  for (int i=sourceStart; i<=sourceStop; i++) {
    if (imageBuf[i] & 0xFF00)
      numCopied++;
  }


  // copy data within image
  memcpy((void*) &(imageBuf[targetStart]), (void*) &(imageBuf[sourceStart]), (sourceStop-sourceStart+1)*sizeof(*imageBuf));
  

  // print message
  if (verbose == INFORM) {
    printf("done\n");
  }
  else if (verbose == CHATTY) {
    if (numCopied>2048)
      printf("done, copied %1.1fkB from 0x%04x - 0x%04x to 0x%04x\n", (float) numCopied/1024.0, sourceStart, sourceStop, targetStart);
    else if (numCopied>0)
      printf("done, copied %dB from 0x%04x - 0x%04x to 0x%04x\n", numCopied, sourceStart, sourceStop, targetStart);
    else
      printf("done, no data copied\n");
  }
  fflush(stdout);
 
} // copy_image

  

/**
   \fn move_image(uint16_t *imageBuf, uint32_t sourceStart, uint32_t sourceStop, uint32_t targetStart, uint8_t verbose)
   
   \param      imageBuf     memory image containing data. HB!=0 indicates content
   \param[in]  sourceStart  starting address to copy from
   \param[in]  sourceStart  last address to copy from
   \param[in]  targetStart  starting address to copy to
   \param[in]  verbose      verbosity level (0=MUTE, 1=SILENT, 2=INFORM, 3=CHATTY)

   Move data section within image to new address. Data at old address is cleared.
*/
void move_image(uint16_t *imageBuf, uint32_t sourceStart, uint32_t sourceStop, uint32_t targetStart, uint8_t verbose) {
  
  uint16_t  *tmpImageBuf;   // temporary buffer

  // print message
  if (verbose == INFORM)
    printf("  move data ... ");
  else if (verbose == CHATTY)
    printf("  move image data ... ");
  fflush(stdout);
  
  // simple checks of scan window
  if (sourceStart > sourceStop)
    Error("source start address 0x%04x higher than end address 0x%04x", sourceStart, sourceStop);
  if (sourceStart > LENIMAGEBUF)
    Error("source start address 0x%04x exceeds buffer size 0x%04x", sourceStart, LENIMAGEBUF);
  if (sourceStop > LENIMAGEBUF)
    Error("source end address 0x%04x exceeds buffer size 0x%04x", sourceStop, LENIMAGEBUF);
  if (targetStart > LENIMAGEBUF)
    Error("target start address 0x%04x exceeds buffer size 0x%04x", targetStart, LENIMAGEBUF);
  if (targetStart+(sourceStop-sourceStart+1) > LENIMAGEBUF)
    Error("target end address 0x%04x exceeds buffer size 0x%04x", targetStart+(sourceStop-sourceStart+1), LENIMAGEBUF);

  // get number of data to move (HB!=0x00)
  int numMoved = 0;
  for (int i=sourceStart; i<=sourceStop; i++) {
    if (imageBuf[i] & 0xFF00)
      numMoved++;
  }

  // allocate temporary buffer (required for overlapping windows
  if (!(tmpImageBuf = malloc(LENIMAGEBUF * sizeof(*tmpImageBuf))))
    Error("Cannot allocate image buffer, try reducing LENIMAGEBUF");

  // copy data from image to temporary buffer
  memcpy((void*) &(tmpImageBuf[sourceStart]), (void*) &(imageBuf[sourceStart]), (sourceStop-sourceStart+1)*sizeof(*imageBuf));

  // clear old data in image
  clear_image(imageBuf, sourceStart, sourceStop, MUTE);

  // copy data from temporary buffer to image
  memcpy((void*) &(imageBuf[targetStart]), (void*) &(tmpImageBuf[sourceStart]), (sourceStop-sourceStart+1)*sizeof(*imageBuf));

  // release temporary buffer again
  free(tmpImageBuf);

  // print message
  if (verbose == INFORM) {
    printf("done\n");
  }
  else if (verbose == CHATTY) {
    if (numMoved>2048)
      printf("done, moved %1.1fkB from 0x%04x - 0x%04x to 0x%04x\n", (float) numMoved/1024.0, sourceStart, sourceStop, targetStart);
    else if (numMoved>0)
      printf("done, moved %dB from 0x%04x - 0x%04x to 0x%04x\n", numMoved, sourceStart, sourceStop, targetStart);
    else
      printf("done, no data copied\n");
  }
  fflush(stdout);
 
} // move_image

  

/**
   \fn void export_s19(char *filename, uint16_t *imageBuf, uint8_t verbose)
   
   \param[in]  filename    name of output file
   \param[in]  imageBuf    memory image. HB!=0 indicates content. Index 0 corresponds to addrStart
   \param[in]  verbose     verbosity level (0=MUTE, 1=SILENT, 2=INFORM, 3=CHATTY)

   export RAM image to file in s19 hexfile format. For description of 
   Motorola S19 file format see http://en.wikipedia.org/wiki/SREC_(file_format)
*/
void export_s19(char *filename, uint16_t *imageBuf, uint8_t verbose) {

  FILE      *fp;               // file pointer
  const int maxLine = 32;      // max. length of data line 
  uint8_t   data;              // value to store
  uint32_t  chk;               // checksum
  uint32_t  addrStart, addrStop, numData;  // image data range
  char      *shortname;        // filename w/o path
    
  // strip path from filename for readability
  #if defined(WIN32)
    shortname = strrchr(filename, '\\');
  #else
    shortname = strrchr(filename, '/');
  #endif
  if (!shortname)
    shortname = filename;
  else
    shortname++;

  // print message
  if (verbose == SILENT)
    printf("  export '%s' ... ", shortname);
  else if (verbose == INFORM)
    printf("  export S19 file '%s' ... ", shortname);
  else if (verbose == CHATTY)
    printf("  export Motorola S19 file '%s' ... ", shortname);
  fflush(stdout);

  // open output file
  fp=fopen(filename,"wb");
  if (!fp)
    Error("Failed to create file %s", filename);

  // start with dummy header line to avoid 'srecord' warning
  fprintf(fp, "S00F000068656C6C6F202020202000003C\n");

  // get min/max addresses and number of bytes (HB!=0x00) in image
  get_image_size(imageBuf, 0, LENIMAGEBUF, &addrStart, &addrStop, &numData);
  
  // store in lines of 32B
  uint32_t addr = addrStart;
  while (addr <= addrStop) {

    // find next data byte (=start address of next block)
    while (((imageBuf[addr] & 0xFF00) == 0) && (addr <= addrStop))
      addr++;
    uint32_t addrBlock = addr;

    // end address reached -> done
    if (addr > addrStop)
      break;

    // set length of next data block: max 128B and align with 128 for speed (see UM0560 section 3.4)  
    int lenBlock = 1; 
    while ((lenBlock < maxLine) && ((addr+lenBlock) <= addrStop) && (imageBuf[addr+lenBlock] & 0xFF00) && ((addr+lenBlock) % maxLine)) {
      lenBlock++;
    }
    //printf("0x%04x   0x%04x   %d\n", addrBlock, addrBlock+lenBlock-1, lenBlock);


    ///////
    // save data in next line, see http://en.wikipedia.org/wiki/SREC_(file_format)
    ///////

    // save data, accound for address width
    if (addrBlock <= 0xFFFF) {
      fprintf(fp, "S1%02X%04X", lenBlock+3, addrBlock);        // 16-bit address: 2B addr + data + 1B chk
      chk = (uint8_t) (lenBlock+3) + (uint8_t) addrBlock + (uint8_t) (addrBlock >> 8);
    }
    else if (addrBlock <= 0xFFFFFF) {
      fprintf(fp, "S2%02X%06X", lenBlock+4, addrBlock);        // 24-bit address: 3B addr + data + 1B chk
      chk = (uint8_t) (lenBlock+4) + (uint8_t) addrBlock + (uint8_t) (addrBlock >> 8) + (uint8_t) (addrBlock >> 16);
    }
    else {
      fprintf(fp, "S3%02X%08X", lenBlock+5, addrBlock);        // 32-bit address: 4B addr + data + 1B chk
      chk = (uint8_t) (lenBlock+5) + (uint8_t) addrBlock + (uint8_t) (addrBlock >> 8) + (uint8_t) (addrBlock >> 16) + (uint8_t) (addrBlock >> 24);
    }
    for (int j=0; j<lenBlock; j++) {
      data = (uint8_t) (imageBuf[addrBlock+j] & 0x00FF);
      chk += data;
      fprintf(fp, "%02X", data);
    }
    chk = ((chk & 0xFF) ^ 0xFF);
    fprintf(fp, "%02X\n", chk);

    // go to next potential block
    addr += lenBlock;

  } // loop over address range 

  // attach generic EOF line
  fprintf(fp, "S903FFFFFE\n");

  // close output file
  fflush(fp);
  fclose(fp);

  // print message
  if ((verbose == SILENT) || (verbose == INFORM)) {
    printf("done\n");
  }
  else if (verbose == CHATTY) {
    if (numData>2048)
      printf("done (%1.1fkB in 0x%04x - 0x%04x)\n", (float) numData/1024.0, addrStart, addrStop);
    else if (numData>0)
      printf("done (%dB in 0x%04x - 0x%04x)\n", numData, addrStart, addrStop);
    else
      printf("done, no data\n");
  }
  fflush(stdout);

} // export_s19
  


/**
   \fn void export_txt(char *filename, uint16_t *imageBuf, uint8_t verbose)
   
   \param[in]  filename    name of output file or stdout ('console')
   \param[in]  imageBuf    memory image. HB!=0 indicates content. Index 0 corresponds to addrStart
   \param[in]  verbose     verbosity level (0=MUTE, 1=SILENT, 2=INFORM, 3=CHATTY)

   export RAM image to file with plain text table (hex addr / hex data)
*/
void export_txt(char *filename, uint16_t *imageBuf, uint8_t verbose) {

  FILE      *fp;               // file pointer
  uint32_t  addrStart, addrStop, numData;  // image data range
  char      *shortname;        // filename w/o path
  bool      flagFile = true;   // output to file or console?
  
  // output to stdout
  if (!strcmp(filename, "console")) {
    flagFile = false;
    fp = stdout;
    if (verbose > MUTE)
      printf("  print memory\n");
    fflush(stdout);
  }

  // output to file
  else {
    flagFile = true;
  
    // strip path from filename for readability
    #if defined(WIN32)
      shortname = strrchr(filename, '\\');
    #else
      shortname = strrchr(filename, '/');
    #endif
    if (!shortname)
      shortname = filename;
    else
      shortname++;

    // print message
    if (verbose == SILENT)
      printf("  export '%s' ... ", shortname);
    else if (verbose == INFORM)
      printf("  export table '%s' ... ", shortname);
    else if (verbose == CHATTY)
      printf("  export ASCII table to file '%s' ... ", shortname);
    fflush(stdout);

    // open output file
    fp=fopen(filename,"wb");
    if (!fp)
      Error("Failed to create file %s", filename);

  } 

  // output header
  if (flagFile)
    fprintf(fp, "# address	value\n");
  else
    fprintf(fp, "    address	value\n");

  // get min/max addresses and number of bytes (HB!=0x00) in image
  get_image_size(imageBuf, 0, LENIMAGEBUF, &addrStart, &addrStop, &numData);

  // output each defined value (HB!=0x00) in a separate line (addr \t value)
  for (int i=addrStart; i<=addrStop; i++) {
    if (imageBuf[i] & 0xFF00) {
      if (!flagFile)
        fprintf(fp,"    ");
      fprintf(fp, "0x%04x	0x%02x\n", i, (imageBuf[i] & 0xFF));
      //printf("0x%04x   0x%04x   0x%02x\n", i, imageBuf[i], (imageBuf[i] & 0xFF));
    }
  }

  // close output file
  fflush(fp);
  if (flagFile)
    fclose(fp);
  else
    fprintf(fp,"  ");
  
  // print message
  if ((verbose == SILENT) || (verbose == INFORM)) {
    printf("done\n");
  }
  else if (verbose == CHATTY) {
    if (numData>2048)
      printf("done (%1.1fkB in 0x%04x - 0x%04x)\n", (float) numData/1024.0, addrStart, addrStop);
    else if (numData>0)
      printf("done (%dB in 0x%04x - 0x%04x)\n", numData, addrStart, addrStop);
    else
      printf("done, no data\n");
  }
  fflush(stdout);

} // export_txt
  


/**
   \fn void export_bin(char *filename, uint16_t *imageBuf, uint8_t verbose)
   
   \param[in]  filename    name of output file
   \param[in]  imageBuf    memory image. HB!=0 indicates content
   \param[in]  verbose     verbosity level (0=MUTE, 1=SILENT, 2=INFORM, 3=CHATTY)

   export RAM image to binary file. Note that start address is not stored, and that
   binary format does not allow for "holes" in the file, i.e. undefined data is stored as 0x00.
*/
void export_bin(char *filename, uint16_t *imageBuf, uint8_t verbose) {

  FILE      *fp;               // file pointer
  uint32_t  addrStart, addrStop, numData;  // address range to consider
  uint32_t  countByte;         // number of actually exported bytes
  uint8_t   val;
  
  // strip path from filename for readability
  #if defined(WIN32)
    const char *shortname = strrchr(filename, '\\');
  #else
    const char *shortname = strrchr(filename, '/');
  #endif
  if (!shortname)
    shortname = filename;
  else
    shortname++;

  // print message
  if (verbose == SILENT)
    printf("  export '%s' ... ", shortname);
  else if (verbose == INFORM)
    printf("  export binary '%s' ... ", shortname);
  else if (verbose == CHATTY)
    printf("  export binary to file '%s' ... ", shortname);
  fflush(stdout);

  // open output file
  fp=fopen(filename,"wb");
  if (!fp)
    Error("Failed to create file %s", filename);

  // get address range containing data (HB!=0x00)
  get_image_size(imageBuf, 0, LENIMAGEBUF, &addrStart, &addrStop, &numData);
  
  // store every value in address range. Undefined values are set to 0x00
  countByte = 0;
  for (uint32_t addr=addrStart; addr<=addrStop; addr++) {
    if (imageBuf[addr] & 0xFF00)
      val = (uint8_t) (imageBuf[addr] & 0x00FF);
    else
      val = 0x00;
    fwrite(&val,sizeof(val), 1, fp); // write byte per byte (image is 16-bit)
    //printf("0x%04x   0x%04x   0x%02x\n", addr, imageBuf[addr], (imageBuf[addr] & 0xFF));
    countByte++;
  }

  // close output file
  fflush(fp);
  fclose(fp);

  // print message
  if ((verbose == SILENT) || (verbose == INFORM)) {
    printf("done\n");
  }
  else if (verbose == CHATTY) {
    if (countByte>2048)
      printf("done (%1.1fkB in 0x%04x - 0x%04x)\n", (float) countByte/1024.0, addrStart, addrStop);
    else if (countByte>0)
      printf("done (%dB in 0x%04x - 0x%04x)\n", countByte, addrStart, addrStop);
    else
      printf("done, no data\n");
  }
  fflush(stdout);

} // export_bin

// end of file
