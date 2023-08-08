/*
 * Copyright (c) 2013-2015, Freescale Semiconductor, Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include "gstallocatorphymem.h"
#include "gstphysmemory.h"

typedef struct
{
  GstMemory mem;
  guint8 *vaddr;
  guint8 *paddr;
  PhyMemBlock block;
} GstMemoryPhy;

static int
default_copy (GstAllocatorPhyMem * allocator, PhyMemBlock * dst_mem,
    PhyMemBlock * src_mem, guint offset, guint size)
{
  GST_WARNING
      ("No default copy implementation for physical memory allocator.\n");
  return -1;
}

static gpointer
gst_phymem_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstMemoryPhy *phymem = (GstMemoryPhy *) mem;

  if (GST_MEMORY_IS_READONLY (mem) && (flags & GST_MAP_WRITE)) {
    GST_ERROR ("memory is read only");
    return NULL;
  }

  return phymem->vaddr;
}

static void
gst_phymem_unmap (GstMemory * mem)
{
  return;
}

static GstMemory *
gst_phymem_copy (GstMemory * mem, gssize offset, gssize size)
{
  GstAllocatorPhyMemClass *klass;
  GstMemoryPhy *src_mem = (GstMemoryPhy *) mem;

  GstMemoryPhy *dst_mem = g_slice_alloc (sizeof (GstMemoryPhy));
  if (dst_mem == NULL) {
    GST_ERROR ("Can't allocate for GstMemoryPhy structure.\n");
    return NULL;
  }

  klass = GST_ALLOCATOR_PHYMEM_CLASS (G_OBJECT_GET_CLASS (mem->allocator));
  if (klass == NULL) {
    GST_ERROR ("Can't get class from allocator object.\n");
    return NULL;
  }

  if (klass->copy_phymem ((GstAllocatorPhyMem *) mem->allocator,
          &dst_mem->block, &src_mem->block, offset, size) < 0) {
    GST_WARNING ("Copy phymem %d failed.\n", size);
    return NULL;
  }

  GST_DEBUG ("copied phymem, vaddr(%p), paddr(%p), size(%d).\n",
      dst_mem->block.vaddr, dst_mem->block.paddr, dst_mem->block.size);

  dst_mem->vaddr = dst_mem->block.vaddr;
  dst_mem->paddr = dst_mem->block.paddr;

  gst_memory_init (GST_MEMORY_CAST (dst_mem),
      mem->mini_object.flags & (~GST_MEMORY_FLAG_READONLY),
      mem->allocator, NULL, mem->maxsize, mem->align, mem->offset, mem->size);

  return (GstMemory *) dst_mem;
}

static GstMemory *
gst_phymem_share (GstMemory * mem, gssize offset, gssize size)
{
  GST_ERROR ("Not implemented mem_share in gstallocatorphymem.\n");
  return NULL;
}

static gboolean
gst_phymem_is_span (GstMemory * mem1, GstMemory * mem2, gsize * offset)
{
  return FALSE;
}

static gpointer
gst_phymem_get_phy (GstMemory * mem)
{
  GstMemoryPhy *phymem = (GstMemoryPhy *) mem;

  return phymem->paddr;
}

static GstMemory *
base_alloc (GstAllocator * allocator, gsize size, GstAllocationParams * params)
{
  GstAllocatorPhyMemClass *klass;
  GstMemoryPhy *mem;
  gsize maxsize, aoffset, offset, align, padding;
  guint8 *data;

  mem = g_slice_alloc (sizeof (GstMemoryPhy));
  if (mem == NULL) {
    GST_ERROR ("Can allocate for GstMemoryPhy structure.\n");
    return NULL;
  }

  klass = GST_ALLOCATOR_PHYMEM_CLASS (G_OBJECT_GET_CLASS (allocator));
  if (klass == NULL) {
    GST_ERROR ("Can't get class from allocator object.\n");
    return NULL;
  }

  GST_DEBUG
      ("allocate params, prefix (%d), padding (%d), align (%d), flags (%x).\n",
      params->prefix, params->padding, params->align, params->flags);

  maxsize = size + params->prefix + params->padding;
  mem->block.size = maxsize;
  if (klass->alloc_phymem ((GstAllocatorPhyMem *) allocator, &mem->block) < 0) {
    GST_ERROR ("Allocate phymem %d failed.\n", maxsize);
    return NULL;
  }

  GST_DEBUG ("allocated phymem, vaddr(%p), paddr(%p), size(%d).\n",
      mem->block.vaddr, mem->block.paddr, mem->block.size);

  data = mem->block.vaddr;
  offset = params->prefix;
  align = params->align;
  /* do alignment */
  if ((aoffset = ((guintptr) data & align))) {
    aoffset = (align + 1) - aoffset;
    data += aoffset;
    maxsize -= aoffset;
  }
  mem->vaddr = mem->block.vaddr + aoffset;
  mem->paddr = mem->block.paddr + aoffset;

  GST_DEBUG ("aligned vaddr(%p), paddr(%p), size(%d).\n",
      mem->block.vaddr, mem->block.paddr, mem->block.size);

  if (offset && (params->flags & GST_MEMORY_FLAG_ZERO_PREFIXED))
    memset (data, 0, offset);

  padding = maxsize - (offset + size);
  if (padding && (params->flags & GST_MEMORY_FLAG_ZERO_PADDED))
    memset (data + offset + size, 0, padding);

  gst_memory_init (GST_MEMORY_CAST (mem), params->flags, allocator, NULL,
      maxsize, align, offset, size);

  return (GstMemory *) mem;
}

static void
base_free (GstAllocator * allocator, GstMemory * mem)
{
  GstAllocatorPhyMemClass *klass;
  GstMemoryPhy *phymem;

  klass = GST_ALLOCATOR_PHYMEM_CLASS (G_OBJECT_GET_CLASS (allocator));
  if (klass == NULL) {
    GST_ERROR ("Can't get class from allocator object, can't free %p\n", mem);
    return;
  }

  phymem = (GstMemoryPhy *) mem;

  GST_DEBUG ("free phymem, vaddr(%p), paddr(%p), size(%d).\n",
      phymem->block.vaddr, phymem->block.paddr, phymem->block.size);

  klass->free_phymem ((GstAllocatorPhyMem *) allocator, &phymem->block);
  g_slice_free1 (sizeof (GstMemoryPhy), mem);

  return;
}

static int
default_alloc (GstAllocatorPhyMem * allocator, PhyMemBlock * phy_mem)
{
  GST_ERROR
      ("No default allocating implementation for physical memory allocation.\n");
  return -1;
}

static int
default_free (GstAllocatorPhyMem * allocator, PhyMemBlock * phy_mem)
{
  GST_ERROR
      ("No default free implementation for physical memory allocation.\n");
  return -1;
}

static guintptr
gst_allocator_phymem_get_phys_addr (GstPhysMemoryAllocator * allocator,
    GstMemory * mem)
{
  return (guintptr)gst_phymem_get_phy (mem);
}

static void
gst_allocator_phymem_iface_init (gpointer g_iface)
{
  GstPhysMemoryAllocatorInterface *iface = g_iface;
  iface->get_phys_addr = gst_allocator_phymem_get_phys_addr;
}

G_DEFINE_TYPE_WITH_CODE (GstAllocatorPhyMem, gst_allocator_phymem,
    GST_TYPE_ALLOCATOR, G_IMPLEMENT_INTERFACE (GST_TYPE_PHYS_MEMORY_ALLOCATOR,
        gst_allocator_phymem_iface_init));

static void
gst_allocator_phymem_class_init (GstAllocatorPhyMemClass * klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = base_alloc;
  allocator_class->free = base_free;
  klass->alloc_phymem = default_alloc;
  klass->free_phymem = default_free;
  klass->copy_phymem = default_copy;
}

static void
gst_allocator_phymem_init (GstAllocatorPhyMem * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_map = gst_phymem_map;
  alloc->mem_unmap = gst_phymem_unmap;
  alloc->mem_copy = gst_phymem_copy;
  alloc->mem_share = gst_phymem_share;
  alloc->mem_is_span = gst_phymem_is_span;
}


//global functions

gboolean
gst_buffer_is_phymem (GstBuffer * buffer)
{
  gboolean ret = FALSE;
  PhyMemBlock *memblk;
  GstMemory *mem = gst_buffer_get_memory (buffer, 0);
  if (mem == NULL) {
    GST_ERROR ("Not get memory from buffer.\n");
    return FALSE;
  }

  if (GST_IS_ALLOCATOR_PHYMEM (mem->allocator)) {
    if (NULL == ((GstMemoryPhy *) mem)->block.paddr) {
      GST_WARNING ("physical address in memory block is invalid");
      ret = FALSE;
    } else {
      ret = TRUE;
    }
  }

  gst_memory_unref (mem);

  return ret;
}

PhyMemBlock *
gst_buffer_query_phymem_block (GstBuffer * buffer)
{
  GstMemory *mem;
  GstMemoryPhy *memphy;
  PhyMemBlock *memblk;

  mem = gst_buffer_get_memory (buffer, 0);
  if (mem == NULL) {
    GST_ERROR ("Not get memory from buffer.\n");
    return NULL;
  }

  if (!GST_IS_ALLOCATOR_PHYMEM (mem->allocator)) {
    gst_memory_unref (mem);
    return NULL;
  }

  memphy = (GstMemoryPhy *) mem;
  memblk = &memphy->block;

  gst_memory_unref (mem);

  return memblk;
}

PhyMemBlock *
gst_memory_query_phymem_block (GstMemory * mem)
{
  GstMemoryPhy *memphy;
  PhyMemBlock *memblk;

  if (!mem)
    return NULL;

  if (!GST_IS_ALLOCATOR_PHYMEM (mem->allocator))
    return NULL;

  memphy = (GstMemoryPhy *) mem;
  memblk = &memphy->block;

  return memblk;
}
