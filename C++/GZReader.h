#pragma once
#define BUFLEN 8192
#include <string>
#include <vector>
#include <zlib.h>
#include <type_traits>
#include <cstdint> // for std::uint32_t, etc.

class GZBuffer
{
private:
	unsigned char buf_in[BUFLEN];
	unsigned int buf_in_pos = 0;
	unsigned char buf_out[BUFLEN];
	z_stream strm = {0};

public:
	std::vector<unsigned char> m_comp; // compressed data

public:
	GZBuffer()
	{
		deflateInit2(&strm, Z_BEST_SPEED, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY);
	}
	virtual ~GZBuffer()
	{
		deflateEnd(&strm);
	}

public:
	void flush()
	{
		// finish deflation if any data remains
		if (buf_in_pos)
		{
			strm.next_in = (Bytef *)buf_in;
			strm.avail_in = buf_in_pos;
			strm.next_out = (Bytef *)buf_out;
			strm.avail_out = BUFLEN;

			deflate(&strm, Z_FINISH);

			auto have = BUFLEN - strm.avail_out;
			m_comp.insert(m_comp.end(), buf_out, buf_out + have);
		}
	}

	size_t size() const
	{
		return m_comp.size();
	}

	bool save(const std::string &path)
	{
		flush();
		if (m_comp.empty())
			return false;
		FILE *f = ::fopen(path.c_str(), "wb");
		if (!f)
			return false;
		bool ret = (fwrite(&m_comp[0], m_comp.size(), 1, f) > 0);
		fclose(f);
		return ret;
	}

	bool write(const void *buf, std::uint32_t len)
	{
		std::uint32_t bufpos = 0;
		while (bufpos < len)
		{
			std::uint32_t copy = BUFLEN - buf_in_pos;
			if (copy > (len - bufpos))
				copy = (len - bufpos);

			memcpy(buf_in + buf_in_pos, (const char *)buf + bufpos, copy);
			bufpos += copy;
			buf_in_pos += copy;

			// if buffer is full, deflate
			if (buf_in_pos == BUFLEN)
			{
				strm.next_in = (Bytef *)buf_in;
				strm.avail_in = BUFLEN;
				strm.next_out = (Bytef *)buf_out;
				strm.avail_out = BUFLEN;

				if (Z_OK != deflate(&strm, Z_NO_FLUSH))
					return false;

				auto have = BUFLEN - strm.avail_out;
				if (have)
					m_comp.insert(m_comp.end(), buf_out, buf_out + have);

				buf_in_pos = 0;
			}
		}
		return true;
	}
	bool write(const double &f)
	{
		return write(&f, sizeof(f));
	}
	bool write(const float &f)
	{
		return write(&f, sizeof(f));
	}
	bool write(unsigned char &b)
	{
		return write(&b, sizeof(b));
	}
	bool write(const std::string &s)
	{
		std::uint32_t len = (std::uint32_t)s.size();
		if (!write(&len, sizeof(len)))
			return false;
		return write(&s[0], len);
	}
	bool opened() const
	{
		return true;
	}
};

class GZWriter
{
public:
	GZWriter(const char *path, const char *mode = "w1b")
	{
		m_fi = gzopen(path, mode);
	}

	virtual ~GZWriter()
	{
		close();
	}

public:
	size_t get_datasize() const
	{
		return gztell(m_fi);
	}
	size_t get_compsize() const
	{
		gzflush(m_fi, Z_FINISH);
		return gzoffset(m_fi) + 20; // approximate overhead
	}

	void close()
	{
		if (m_fi)
			gzclose(m_fi);
		m_fi = nullptr;
	}

protected:
	gzFile m_fi;

public:
	bool write(const std::string &s)
	{
		return gzwrite(m_fi, s.data(), (unsigned)s.size()) > 0;
	}
	bool write(const void *buf, std::uint32_t len)
	{
		return gzwrite(m_fi, buf, len) > 0;
	}
	bool write(const double &f)
	{
		return write(&f, sizeof(f));
	}
	bool write(const float &f)
	{
		return write(&f, sizeof(f));
	}
	bool write(unsigned char &b)
	{
		return write(&b, sizeof(b));
	}
	bool write(char &c)
	{
		return write(&c, sizeof(c));
	}
	bool write(short &s)
	{
		return write(&s, sizeof(s));
	}
	bool write(unsigned short &v)
	{
		return write(&v, sizeof(v));
	}
	bool write(long &b)
	{
		return write(&b, sizeof(b));
	}
	bool write(unsigned long &b)
	{
		return write(&b, sizeof(b));
	}
	bool write_with_len(const std::string &s)
	{
		std::uint32_t len = (std::uint32_t)s.size();
		if (!write(&len, sizeof(len)))
			return false;
		return write(s.data(), len);
	}

	bool opened() const
	{
		return (m_fi != nullptr);
	}
};

class GZReader
{
public:
	GZReader(const char *path)
	{
		m_fi = gzopen(path, "rb");
	}

	virtual ~GZReader()
	{
		if (m_fi)
			gzclose(m_fi);
	}

protected:
	gzFile m_fi;
	std::uint32_t fi_remain = 0; // how many bytes are left in fi_buf
	unsigned char fi_buf[BUFLEN];
	const unsigned char *fi_ptr = fi_buf;

public:
	// read up to 'len' bytes into 'dest'; returns how many bytes were read
	std::uint32_t read(void *dest, std::uint32_t len)
	{
		if (!dest)
			return 0;

		unsigned char *buf = (unsigned char *)dest;
		std::uint32_t nread = 0;

		while (len > 0)
		{
			if (len <= fi_remain)
			{
				// enough buffered data to satisfy 'len'
				memcpy(buf, fi_ptr, len);
				fi_remain -= len;
				fi_ptr += len;
				nread += len;
				break;
			}
			else if (fi_remain > 0)
			{
				// consume all fi_remain
				memcpy(buf, fi_ptr, fi_remain);
				buf += fi_remain;
				nread += fi_remain;
				len -= fi_remain;
				fi_remain = 0;
			}
			// need to read more from file
			int unzippedBytes = gzread(m_fi, fi_buf, BUFLEN);
			if (unzippedBytes <= 0)
				return nread; // end of file or error
			fi_remain = (std::uint32_t)unzippedBytes;
			fi_ptr = fi_buf;
		}
		return nread;
	}

	bool skip(std::uint32_t len)
	{
		// skip 'len' bytes (no need to track remain externally)
		if (len <= fi_remain)
		{
			fi_remain -= len;
			fi_ptr += len;
			return true;
		}
		else
		{
			// reduce from what we have left
			if (fi_remain)
			{
				len -= fi_remain;
				fi_remain = 0;
			}
			// now seek forward in the gzip
			z_off_t nskip = gzseek(m_fi, len, SEEK_CUR);
			return (nskip != -1);
		}
	}

	bool skip(std::uint32_t len, std::uint32_t &remain)
	{
		// same as above, but also reduce 'remain'
		if (remain < len)
			return false;
		if (len <= fi_remain)
		{
			fi_remain -= len;
			fi_ptr += len;
			remain -= len;
			return true;
		}
		else
		{
			if (fi_remain)
			{
				len -= fi_remain;
				remain -= fi_remain;
				fi_remain = 0;
			}
			z_off_t nskip = gzseek(m_fi, len, SEEK_CUR);
			if (nskip == -1)
				return false;
			remain -= len;
			return true;
		}
	}

	template <typename R>
	bool skip(std::uint32_t len, R &remain)
	{
		// let R be integral (uint32_t, etc.)
		static_assert(std::is_integral<R>::value, "remain must be integral");
		if (remain < len)
			return false;
		if (len <= fi_remain)
		{
			fi_remain -= len;
			fi_ptr += len;
			remain -= len;
			return true;
		}
		else
		{
			if (fi_remain)
			{
				len -= fi_remain;
				remain -= fi_remain;
				fi_remain = 0;
			}
			z_off_t nskip = gzseek(m_fi, len, SEEK_CUR);
			if (nskip == -1)
				return false;
			remain -= len;
			return true;
		}
	}

	// Overloads to fetch numeric types from the compressed stream,
	// also decreasing 'remain'.
	bool fetch(std::int32_t &x, std::uint32_t &remain)
	{
		if (remain < sizeof(x))
			return false;
		std::uint32_t nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}
	bool fetch(std::uint32_t &x, std::uint32_t &remain)
	{
		if (remain < sizeof(x))
			return false;
		std::uint32_t nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}
	bool fetch(std::int16_t &x, std::uint32_t &remain)
	{
		if (remain < sizeof(x))
			return false;
		std::uint32_t nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}
	bool fetch(std::uint16_t &x, std::uint32_t &remain)
	{
		if (remain < sizeof(x))
			return false;
		std::uint32_t nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}
	bool fetch(float &x, std::uint32_t &remain)
	{
		if (remain < sizeof(x))
			return false;
		std::uint32_t nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}
	bool fetch(double &x, std::uint32_t &remain)
	{
		if (remain < sizeof(x))
			return false;
		std::uint32_t nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}
	bool fetch(std::uint8_t &x, std::uint32_t &remain)
	{
		if (remain < sizeof(x))
			return false;
		std::uint32_t nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}
	bool fetch(char &x, std::uint32_t &remain)
	{
		if (remain < sizeof(x))
			return false;
		std::uint32_t nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}

	// A fetch that reads a length-4-bytes and then that many bytes into a string
	bool fetch(std::string &x, std::uint32_t &remain)
	{
		std::uint32_t strlen32 = 0;
		if (!fetch(strlen32, remain))
			return false;
		if (strlen32 >= 1048576)
			return false; // sanity check

		// read 'strlen32' bytes
		x.resize(strlen32);
		if (remain < strlen32)
			return false;
		std::uint32_t nread = read(&x[0], strlen32);
		remain -= nread;
		return (nread == strlen32);
	}

	// This version is used in your code to parse a “string with length”:
	bool fetch_with_len(std::string &x, std::uint32_t &remain)
	{
		// read 4 bytes for length
		std::uint32_t strLen32 = 0;
		if (!fetch(strLen32, remain))
			return false;

		if (strLen32 >= 1048576)
			return false; // sanity check

		x.resize(strLen32);
		if (remain < strLen32)
			return false;
		std::uint32_t nread = read(&x[0], strLen32);
		remain -= nread;
		return (nread == strLen32);
	}

	bool opened() const
	{
		return (m_fi != nullptr);
	}

	bool eof() const
	{
		// if gzread has returned 0 or negative and no leftover in fi_remain, we are at EOF
		return (gzeof(m_fi) && (fi_remain == 0));
	}

	void rewind()
	{
		gzrewind(m_fi);
		fi_remain = 0;
		fi_ptr = fi_buf;
	}
};

// A simple buffer class (unchanged except for 32-bit adjustments if needed)
class BUF : public std::vector<unsigned char>
{
	std::uint32_t pos = 0;

public:
	BUF(std::uint32_t len = 0) : std::vector<unsigned char>(len) { pos = 0; }

	void skip(std::uint32_t len)
	{
		pos += len;
	}
	void skip_str()
	{
		std::uint32_t strlen;
		if (!fetch(strlen))
			return;
		pos += strlen;
	}
	bool fetch(void *p, std::uint32_t len)
	{
		if (this->size() < pos + len)
		{
			pos = (std::uint32_t)this->size();
			return false;
		}
		memcpy(p, &(*this)[pos], len);
		pos += len;
		return true;
	}
	bool fetch(std::uint32_t &x)
	{
		if (this->size() < pos + sizeof(x))
		{
			pos = (std::uint32_t)this->size();
			return false;
		}
		memcpy(&x, &(*this)[pos], sizeof(x));
		pos += sizeof(x);
		return true;
	}
	bool fetch(float &x)
	{
		if (this->size() < pos + sizeof(x))
		{
			pos = (std::uint32_t)this->size();
			return false;
		}
		memcpy(&x, &(*this)[pos], sizeof(x));
		pos += sizeof(x);
		return true;
	}
	bool fetch(double &x)
	{
		if (this->size() < pos + sizeof(x))
		{
			pos = (std::uint32_t)this->size();
			return false;
		}
		memcpy(&x, &(*this)[pos], sizeof(x));
		pos += sizeof(x);
		return true;
	}
	bool fetch_with_len(std::string &x)
	{
		std::uint32_t strlen = 0;
		if (!fetch(strlen))
			return false;
		if (strlen >= 1048576)
			return false;
		if (this->size() < pos + strlen)
		{
			pos = (std::uint32_t)this->size();
			return false;
		}
		x.resize(strlen);
		memcpy(&x[0], &(*this)[pos], strlen);
		pos += strlen;
		return true;
	}
};
