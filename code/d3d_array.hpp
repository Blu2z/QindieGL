/***************************************************************************
* Copyright (C) 2011-2016, Crystice Softworks.
* 
* This file is part of QindieGL source code.
* Please note that QindieGL is not driver, it's emulator.
* 
* QindieGL source code is free software; you can redistribute it and/or 
* modify it under the terms of the GNU General Public License as 
* published by the Free Software Foundation; either version 2 of 
* the License, or (at your option) any later version.
* 
* QindieGL source code is distributed in the hope that it will be 
* useful, but WITHOUT ANY WARRANTY; without even the implied 
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
* See the GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software 
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
***************************************************************************/
#ifndef	QINDIEGL_D3D_ARRAY_H
#define QINDIEGL_D3D_ARRAY_H

// Draws are streamed through one dynamic vertex buffer and one dynamic index
// buffer per index size, used as rings: each draw appends with
// D3DLOCK_NOOVERWRITE, and only a draw that does not fit restarts the ring
// with D3DLOCK_DISCARD. Discarding on every draw made the driver rename the
// buffers thousands of times per frame.
class D3DVABuffer
{
public:
	D3DVABuffer();
	~D3DVABuffer();
	void Lock( GLint first, GLint last );
	void Unlock();
	template<typename T> void SetIndices( GLenum mode, GLuint start, GLuint end, GLsizei count, const T *indices );
	void DrawPrimitive();

	GLint GetLockFirst() const { return m_lockFirst; }
	GLsizei GetLockCount() const { return m_lockCount; }

protected:
	// Lock space for count vertices of m_vertexSize floats; sets m_baseVertex.
	GLfloat *LockVertices( GLsizei count );
	// Selects the 16- or 32-bit ring and locks space for numIndices indices;
	// sets m_indexSize and m_startIndex. Returns the ring (0 or 1), or -1.
	int LockIndices( GLsizei numIndices, GLuint maximumIndex, GLvoid **locked );
	void SetupTexCoords( const float *texcoords, int num_coords, const float *position,
		const float *normal, int stage, const D3DXMATRIX *softwareTransform,
		const D3DXMATRIX *projectiveTransform, float *out_texcoords );

	inline void SetIndex( void *pDest, GLuint dstIndex, GLsizei srcIndex )
	{
		if (m_indexSize == 4)
			*((GLuint*)pDest + dstIndex) = (GLuint)srcIndex;
		else
			*((GLushort*)pDest + dstIndex) = (GLushort)srcIndex;
	}
	template<typename T>
	inline void SetIndex( void *pDest, GLuint dstIndex, T srcIndex )
	{
		if (m_indexSize == 4)
			*((GLuint*)pDest + dstIndex) = (GLuint)srcIndex;
		else
			*((GLushort*)pDest + dstIndex) = (GLushort)srcIndex;
	}

private:
	LPDIRECT3DVERTEXBUFFER9		m_pVertexBuffer;
	LPDIRECT3DINDEXBUFFER9		m_pIndexBuffer[2];	// 16-bit, 32-bit
	UINT						m_vbCapacity;		// bytes
	UINT						m_vbOffset;			// next free byte
	UINT						m_ibCapacity[2];
	UINT						m_ibOffset[2];
	GLsizei						m_vertexSize;		// floats per vertex
	GLsizei						m_indexSize;		// bytes per index
	GLint						m_lockFirst;
	GLsizei						m_lockCount;
	UINT						m_baseVertex;		// ring position of the locked vertices
	UINT						m_startIndex;		// ring position of the locked indices
	GLenum						m_primitiveType;
	GLsizei						m_primitiveIndexCount;
};

#endif //QINDIEGL_D3D_ARRAY_H
