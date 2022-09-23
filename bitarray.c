/**
 * Copyright (c) 2012 MIT License by 6.172 Staff
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 **/

// Implements the ADT specified in bitarray.h as a packed array of bits; a bit
// array containing bit_sz bits will consume roughly bit_sz/8 bytes of
// memory.
#include "./bitarray.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include <sys/types.h>
#include <stdio.h>

// ********************************* Types **********************************

// Concrete data type representing an array of bits.
struct bitarray {
  // The number of bits represented by this bit array.
  // Need not be divisible by 8.
  size_t bit_sz;

  // The underlying memory buffer that stores the bits in
  // packed form (8 per byte).
  char* buf;
};


// ******************** Prototypes for static functions *********************

// Rotates a subarray left by an arbitrary number of bits.
//
// bit_offset is the index of the start of the subarray
// bit_length is the length of the subarray, in bits
// bit_left_amount is the number of places to rotate the
//                    subarray left
//
// The subarray spans the half-open interval
// [bit_offset, bit_offset + bit_length)
// That is, the start is inclusive, but the end is exclusive.
static void bitarray_rotate_left(bitarray_t* const bitarray,
                                 const size_t bit_offset,
                                 const size_t bit_length,
                                 const size_t bit_left_amount);

// Rotates a subarray left by one bit.
//
// bit_offset is the index of the start of the subarray
// bit_length is the length of the subarray, in bits
//
// The subarray spans the half-open interval
// [bit_offset, bit_offset + bit_length)
// That is, the start is inclusive, but the end is exclusive.
static void bitarray_rotate_left_one(bitarray_t* const bitarray,
                                     const size_t bit_offset,
                                     const size_t bit_length);

// Portable modulo operation that supports negative dividends.
//
// Many programming languages define modulo in a manner incompatible with its
// widely-accepted mathematical definition.
// http://stackoverflow.com/questions/1907565/c-python-different-behaviour-of-the-modulo-operation
// provides details; in particular, C's modulo
// operator (which the standard calls a "remainder" operator) yields a result
// signed identically to the dividend e.g., -1 % 10 yields -1.
// This is obviously unacceptable for a function which returns size_t, so we
// define our own.
//
// n is the dividend and m is the divisor
//
// Returns a positive integer r = n (mod m), in the range
// 0 <= r < m.
static size_t modulo(const ssize_t n, const size_t m);

// Produces a mask which, when ANDed with a byte, retains only the
// bit_index th byte.
//
// Example: bitmask(5) produces the byte 0b00100000.
//
// (Note that here the index is counted from right
// to left, which is different from how we represent bitarrays in the
// tests.  This function is only used by bitarray_get and bitarray_set,
// however, so as long as you always use bitarray_get and bitarray_set
// to access bits in your bitarray, this reverse representation should
// not matter.
static char bitmask(const size_t bit_index);

//
static void reverse(bitarray_t* const bitarray, const size_t left, const size_t right);
static void reverse_byte(bitarray_t* const bitarray, const size_t byteleft, const size_t byteright);
static void reverse_short(bitarray_t* const bitarray, const size_t left, const size_t right);
static void bitarray_rotate_short(bitarray_t* const bitarray,
                                 const size_t l,
                                 const size_t r,
                                 const size_t bit_left_amount); //bit_left_amount <=8
static void bitarray_rotate_short_right(bitarray_t* const bitarray,
                                 const size_t l,
                                 const size_t r,
                                 const size_t bit_right_amount); //bit_right_amount <=8
// ******************************* Functions ********************************

bitarray_t* bitarray_new(const size_t bit_sz) {
  // Allocate an underlying buffer of ceil(bit_sz/8) bytes.
  char* const buf = calloc(1, (bit_sz+7) / 8);
  if (buf == NULL) {
    return NULL;
  }

  // Allocate space for the struct.
  bitarray_t* const bitarray = malloc(sizeof(struct bitarray));
  if (bitarray == NULL) {
    free(buf);
    return NULL;
  }

  bitarray->buf = buf;
  bitarray->bit_sz = bit_sz;
  return bitarray;
}

void bitarray_free(bitarray_t* const bitarray) {
  if (bitarray == NULL) {
    return;
  }
  free(bitarray->buf);
  bitarray->buf = NULL;
  free(bitarray);
}

size_t bitarray_get_bit_sz(const bitarray_t* const bitarray) {
  return bitarray->bit_sz;
}

bool bitarray_get(const bitarray_t* const bitarray, const size_t bit_index) {
  assert(bit_index < bitarray->bit_sz);

  // We're storing bits in packed form, 8 per byte.  So to get the nth
  // bit, we want to look at the (n mod 8)th bit of the (floor(n/8)th)
  // byte.
  //
  // In C, integer division is floored explicitly, so we can just do it to
  // get the byte; we then bitwise-and the byte with an appropriate mask
  // to produce either a zero byte (if the bit was 0) or a nonzero byte
  // (if it wasn't).  Finally, we convert that to a boolean.
  return (bitarray->buf[bit_index / 8] & bitmask(bit_index)) ?
         true : false;
}

void bitarray_set(bitarray_t* const bitarray,
                  const size_t bit_index,
                  const bool value) {
  assert(bit_index < bitarray->bit_sz);

  // We're storing bits in packed form, 8 per byte.  So to set the nth
  // bit, we want to set the (n mod 8)th bit of the (floor(n/8)th) byte.
  //
  // In C, integer division is floored explicitly, so we can just do it to
  // get the byte; we then bitwise-and the byte with an appropriate mask
  // to clear out the bit we're about to set.  We bitwise-or the result
  // with a byte that has either a 1 or a 0 in the correct place.
  bitarray->buf[bit_index / 8] =
    (bitarray->buf[bit_index / 8] & ~bitmask(bit_index)) |
    (value ? bitmask(bit_index) : 0);
}

void bitarray_randfill(bitarray_t* const bitarray){
  int32_t *ptr = (int32_t *)bitarray->buf;
  for (int64_t i=0; i<bitarray->bit_sz/32 + 1; i++){
    ptr[i] = rand();
  }
}

void bitarray_rotate(bitarray_t* const bitarray,
                     const size_t bit_offset,
                     const size_t bit_length,
                     const ssize_t bit_right_amount) {
  assert(bit_offset + bit_length <= bitarray->bit_sz);
  int bit_left_amount=modulo(-bit_right_amount, bit_length);
  if (bit_length == 0 || bit_left_amount==0) {
    return;
  }
  // Convert a rotate left or right to a left rotate only, and eliminate
  // multiple full rotations.
  bitarray_rotate_left(bitarray, bit_offset, bit_length,bit_left_amount);
}

static void bitarray_rotate_left(bitarray_t* const bitarray,
                                 const size_t bit_offset,
                                 const size_t bit_length,
                                 const size_t bit_left_amount) {
  size_t i,j,l,m,r,tmp,prev;
  size_t k;
  l=bit_offset; m=bit_offset+bit_left_amount; r=bit_offset+bit_length-1;
  if(bit_left_amount==0) return;
  /*for(i=l;i<=r-bit_left_amount;i++){
    if(i<bit_left_amount+l)
      k=i+r+1-bit_left_amount-l;
    else
      k=i-bit_left_amount;
    tmp=bitarray_get(bitarray,i);
    bitarray_set(bitarray,i,bitarray_get(bitarray,k));
    bitarray_set(bitarray,k,tmp);
  }
  return;*/
  //printf("\n");
  //for(size_t i=l; i<=r; i++){
  //  tmp=bitarray_get(bitarray, i);
  //  printf("%d",tmp);
  //}
  //printf("\n");
  //printf("%d %d\n",l,r);
  /*i=r;
  prev=bitarray_get(bitarray,r);
  do{
    k=i-bit_left_amount;
    if(k<l)
      k=k+r+1;
    tmp=bitarray_get(bitarray,k);
    bitarray_set(bitarray,k,prev);
    prev=tmp;
    i=k;
  }while(i!=r);
  for(i=0;i<bit_left_amount;i++){
    prev=bitarray_get(bitarray,r-i);     
    for(j=r-i;j>=l;j-=bit_left_amount){
      k=j-bit_left_amount;
      if(k<l)
        k=k+r+1;
      tmp=bitarray_get(bitarray,k);
      bitarray_set(bitarray,k,prev);
      prev=tmp;
    }
  }*/
  reverse(bitarray,l,m-1);
  reverse(bitarray,m,r);
  reverse(bitarray,l,r);
  /*printf("\n");
  for(size_t i=l; i<=r; i++){
    tmp=bitarray_get(bitarray, i);
    printf("%d",tmp);
  }
  printf("\n");
  printf("%d %d\n",l,r);*/
}

static void reverse_byte(bitarray_t* const bitarray, const size_t byteleft, const size_t byteright){
  size_t i,j;
  char tmp;
  for(i=byteleft,j=byteright;i<j;i++,j--){
    tmp=bitarray->buf[i];
    bitarray->buf[i]=bitarray->buf[j];
    bitarray->buf[j]=tmp;
    reverse_short(bitarray,8*i,8*i+7);
    reverse_short(bitarray,8*j,8*j+7);
  }
}
static void reverse_short(bitarray_t* const bitarray, const size_t left, const size_t right){//right-left<8,left and right belong to the same byte
  size_t i,j;
  char byte;
  bool tmp;
  byte=bitarray->buf[left>>3];
  /*for(i=left,j=right;i<j;i++,j--){
    tmp=bitarray_get(bitarray, i);
    bitarray_set(bitarray,i,bitarray_get(bitarray,j));
    bitarray_set(bitarray,j,tmp);
  }*/
  for(i=left,j=right;i<j;i++,j--){
    tmp=(byte & bitmask(i)) ? true : false;
    bitarray_set(bitarray,i,(byte & bitmask(i)) ? true : false);
    bitarray_set(bitarray,j,tmp);
  }
}
static void reverse(bitarray_t* const bitarray, const size_t left, const size_t right){
  size_t i,j,l,k;
  int8_t byte1,byte2,ci,offset,wholel,wholer;
  //bool biti,bitj;
  wholel=((left+7)/8)*8;
  wholer=right-right%8;
  byte1=bitarray->buf[left/8];
  byte2=bitarray->buf[right/8];
  reverse_short(bitarray,left,wholel-1);
  reverse_short(bitarray,wholer,right);
  reverse_byte(bitarray,wholel/8,wholer/8-1);
  /*if(wholel-left<right-wholer){
    bitarray_rotate_short_right(bitarray,left,right,right+left-wholer-wholel);
  }
  else
    if(wholel-left>right-wholer)
      bitarray_rotate_short(bitarray,left,right,wholel+wholer-right-left);*/
  //to do: swap before and after, copy back the correct bits between the first byte and left, right and last byte
  /*k=left/8,l=right/8;
  offset=(right+left)%8;
  byte1=bitarray->buf[k];
  byte2=bitarray->buf[l];
  ci=left%8;
  if(offset==0){
    for(i=left,j=right;i<j;){
      byte1=bitarray->buf[i>>3];
      byte2=bitarray->buf[j>>3];
      for(; ci<8 && i<j; ci++,i++,j--){
        bitarray_set(bitarray,i,byte2&bitmask(j));
        bitarray_set(bitarray,j,byte1&bitmask(i));
      }
      ci=0;
    }
  }
  else{
    //i=left,j=right;
    //while(i<j){
    //tmp=bitarray_get(bitarray, i);
     //  byte1=bitarray->buf[i>>3];
      //biti=byte1&bitmask(i);
      for(i=left,j=right; i<j; ci++,i++,j--){
        if(ci==8){
          byte1=bitarray->buf[i>>3];
          ci=0;
        }
        else
          if(ci==offset)
           byte2=bitarray->buf[j>>3];
        bitarray_set(bitarray,i,byte2&bitmask(j));
        bitarray_set(bitarray,j,byte1&bitmask(i));
      }
      //ci=0;
      //byte1=bitarray->buf[i>>3];
    //}
  }
  
  bytel=left-left%8;
  byter=right+7-right%8;
  printf("a%d %d %d %d\n",left,right,bytel,byter);
  for(i=bytel/8,j=byter/8; i<j; i++,j--){
    tmp=bitarray->buf[i];
    printf("%c\n",tmp);
    bitarray->buf[i ]=bitarray->buf[(j)];
    bitarray->buf[(j )]=tmp;
  }
  //printf("%d %d\n",i,j);
  for(i=bytel;i<=byter;i+=8){
    //if(i+8<byter)
      k=i+7;
    //else k=right;
    printf("%d\n",i);
    for(j=i;j<k;j++,k--){
      tmp=bitarray_get(bitarray, j);
      bitarray_set(bitarray,j,bitarray_get(bitarray,k));
      bitarray_set(bitarray,k,tmp);
    }
  }
  for(j=bytel,k=left-1;j<k;j++,k--){
    tmp=bitarray_get(bitarray, j);
    bitarray_set(bitarray,j,bitarray_get(bitarray,k));
    bitarray_set(bitarray,k,tmp);
  }
  for(j=right,k=byter;j<k;j++,k--){
    tmp=bitarray_get(bitarray, j);
    bitarray_set(bitarray,j,bitarray_get(bitarray,k));
    bitarray_set(bitarray,k,tmp);
  }*/
}

static void bitarray_rotate_short(bitarray_t* const bitarray,
                                 const size_t l,
                                 const size_t r,
                                 const size_t bit_left_amount) {
  unsigned int current;
  size_t i,rbyte;
  char mask,prev,k; 
   int j;
  rbyte=r>>3;
  for(i=0,mask=0; i<8-bit_left_amount; i++)
    mask|=1<<i;
  prev=bitarray->buf[l>>3]&mask;
  /*for(i=(l>>3)+1;i<rbyte;i+=3){
    current+=(((unsigned int)prev)<<24);
    current+=(((unsigned int)bitarray[i])<<(16+bit_left_amount));
    current+=(((unsigned int)bitarray[i+1])<<(8+bit_left_amount));
    current+=(((unsigned int)bitarray[i+2])<<bit_left_amount);
    current+=(((unsigned int)bitarray[i+3])>>bit_left_amount);
    prev=bitarray[i+3]&mask;
    for(j=0;j<3;j++){
      k=(8*(3-j));
      bitarray[i+j]=current>>k;
      current-=(bitarray[i+j]>>k)<<k;
    }
  }*/
  current=0;
  for(i=(l>>3)+1,j=16+bit_left_amount;i<=rbyte;i++,j-=8){
    current+=(((unsigned int)bitarray->buf[i])<<j);
    if(j<0 || i==rbyte){      
      current+=(((unsigned int)prev)<<24);
      current+=(((unsigned int)bitarray->buf[i])>>(8-bit_left_amount));
      prev=bitarray->buf[i]&mask;
      for(j=0,k=24;j<3;j++,k-=8){
        bitarray->buf[i+j-3]=current>>k;
        current-=((current>>k)<<k);
      }
      j=16+bit_left_amount;
      current=0;
    }
  }
} 
static void bitarray_rotate_short_right(bitarray_t* const bitarray,
                                 const size_t l,
                                 const size_t r,
                                 const size_t bit_right_amount) {
  unsigned int current;
  size_t i,rbyte;
  char mask,prev,k;
   int j;
  rbyte=r>>3;
  for(i=0,mask=0; i<bit_right_amount; i++)
    mask|=1<<i;
  prev=bitarray->buf[l>>3]&mask;
  current=0;
  for(i=(l>>3),j=24-bit_right_amount;i<=rbyte;i++,j-=8){
    current+=(((unsigned int)bitarray->buf[i])<<j);
    if(j==8-bit_right_amount || i==rbyte){
      current+=(((unsigned int)prev)<<24);
      current+=(((unsigned int)bitarray->buf[i])>>bit_right_amount);
      prev=bitarray->buf[i]&mask;
      for(j=0,k=24;j<3;j++,k-=8){
        bitarray->buf[i+j-3]=current>>k;
        current-=((current>>k)<<k);  
        }
      j=24-bit_right_amount;
      current=0;
    }
  }
}
/*
static void bitarray_rotate_left(bitarray_t* const bitarray,
                                 const size_t bit_offset,
                                 const size_t bit_length,
                                 const size_t bit_left_amount) {
  for (size_t i = 0; i < bit_left_amount; i++) {
    bitarray_rotate_left_one(bitarray, bit_offset, bit_length);
  }
}
*/
static void bitarray_rotate_left_one(bitarray_t* const bitarray,
                                     const size_t bit_offset,
                                     const size_t bit_length) {
  // Grab the first bit in the range, shift everything left by one, and
  // then stick the first bit at the end.
  const bool first_bit = bitarray_get(bitarray, bit_offset);
  size_t i;
  for (i = bit_offset; i + 1 < bit_offset + bit_length; i++) {
    bitarray_set(bitarray, i, bitarray_get(bitarray, i + 1));
  }
  bitarray_set(bitarray, i, first_bit);
}

static size_t modulo(const ssize_t n, const size_t m) {
  const ssize_t signed_m = (ssize_t)m;
  assert(signed_m > 0);
  const ssize_t result = ((n % signed_m) + signed_m) % signed_m;
  assert(result >= 0);
  return (size_t)result;
}

static char bitmask(const size_t bit_index) {
  return 1 << (bit_index % 8);
}

