#pragma once
#define BUFLEN 8192
#include <string>
#include <vector>
#include <zlib.h>
#include <type_traits>
using namespace std;

class GZBuffer
{
private:
	unsigned char buf_in[BUFLEN];
	unsigned int buf_in_pos = 0;
	unsigned char buf_out[BUFLEN];
	z_stream strm = {
		0,
	};

public:
	vector<unsigned char> m_comp; // ����� ���

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
		// ���� ���۸� ����
		if (buf_in_pos)
		{
			strm.next_in = (Bytef *)buf_in;
			strm.avail_in = buf_in_pos;
			strm.next_out = (Bytef *)buf_out;
			strm.avail_out = BUFLEN;

			// ���� ����
			deflate(&strm, Z_FINISH);

			// ���� ���� ������
			auto have = BUFLEN - strm.avail_out;
			m_comp.insert(m_comp.end(), buf_out, buf_out + have);
		}
	}

	size_t size() const
	{
		return m_comp.size();
	}

	bool save(const string &path)
	{
		flush();

		if (m_comp.empty())
			return false;
		auto f = ::fopen(path.c_str(), "wb");
		if (!f)
			return false;
		bool ret = (fwrite(&m_comp[0], m_comp.size(), 1, f) > 0);
		fclose(f);
		return ret;
	}

	bool write(const void *buf, unsigned long len)
	{
		unsigned long bufpos = 0;
		while (bufpos < len)
		{
			auto copy = BUFLEN - buf_in_pos; // �̹��� �ִ� ������ �� �ִ� ��
			if (copy > len - bufpos)
			{						 // ���� �����Ͱ� �׸�ŭ ������
				copy = len - bufpos; // ���縸 �ϰ� ������ ������
			}

			// �ش� ���� ������
			memcpy(buf_in + buf_in_pos, (char *)buf + bufpos, copy);
			bufpos += copy;
			buf_in_pos += copy;

			// ���۰� �� á��
			if (buf_in_pos == BUFLEN)
			{ // ���� ����
				strm.next_in = (Bytef *)buf_in;
				strm.avail_in = BUFLEN;
				strm.next_out = (Bytef *)buf_out;
				strm.avail_out = BUFLEN;
				if (Z_OK != deflate(&strm, Z_NO_FLUSH))
					return false;

				// ���� ������ (���������Ƿ� buflen �����̴�)
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
	bool write(const string &s)
	{
		unsigned long len = s.size();
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
		return gzoffset(m_fi) + 20; // 20����Ʈ ���
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
	bool write(const string &s)
	{
		return gzwrite(m_fi, &s[0], s.size()) > 0;
	}
	bool write(const void *buf, unsigned long len)
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
	bool write_with_len(const string &s)
	{
		unsigned long len = s.size();
		if (!write(&len, sizeof(len)))
			return false;
		return write(&s[0], len);
	}

		bool opened() const
	{
		return m_fi != 0;
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
	unsigned long fi_remain = 0;		  // ������ �����Ƿ�
	unsigned char fi_buf[BUFLEN];		  // ���� ���� ������
	const unsigned char *fi_ptr = fi_buf; // fi_buf���� ���� ���� ������
										  // ��Ȯ�� len byte�� ����. �� ������ true, 1����Ʈ�� �������� false�� ����
public:
	unsigned long read(void *dest, unsigned long len)
	{
		unsigned char *buf = (unsigned char *)dest;
		if (!buf)
			return 0;
		unsigned long nread = 0;
		while (len > 0)
		{
			if (len <= fi_remain)
			{							  // �����͸� �� �о ����ϸ�?
				memcpy(buf, fi_ptr, len); // �����ϰ� ����
				fi_remain -= len;
				fi_ptr += len;
				nread += len;
				break;
			}
			else if (fi_remain)
			{									// �����ϸ�?
				memcpy(buf, fi_ptr, fi_remain); // �ϴ� �ִ°��� �� ����
				len -= fi_remain;
				buf += fi_remain;
				nread += fi_remain;
			}
			unsigned long unzippedBytes = gzread(m_fi, fi_buf, BUFLEN); // �߰��� �о����
			if (!unzippedBytes)
				return nread; // ���̻� ������ ������
			fi_remain = unzippedBytes;
			fi_ptr = fi_buf;
		}
		return nread;
	}

	bool skip(unsigned long len)
	{
		if (len <= fi_remain)
		{
			fi_remain -= len;
			fi_ptr += len;
			return true;
		}
		else if (fi_remain)
		{
			len -= fi_remain;
			fi_remain = 0;
		}
		return -1 != gzseek(m_fi, len, SEEK_CUR);
	}

	bool skip(unsigned long len, unsigned long &remain)
	{
		if (remain < len)
			return false;
		if (len <= fi_remain)
		{
			fi_remain -= len;
			fi_ptr += len;
			remain -= len;
			return true;
		}
		else if (fi_remain)
		{
			len -= fi_remain;
			remain -= fi_remain;
			fi_remain = 0;
		}

		z_off_t nskip = gzseek(m_fi, len, SEEK_CUR);
		if (-1 == nskip)
			return false;
		remain -= len;
		return true;
	}

	template <typename R>
	bool skip(unsigned long len, R &remain)
	{
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
		else if (fi_remain)
		{
			len -= fi_remain;
			remain -= fi_remain;
			fi_remain = 0;
		}

		z_off_t nskip = gzseek(m_fi, len, SEEK_CUR);
		if (-1 == nskip)
			return false;
		remain -= len;
		return true;
	}

	// x�� �а� remain�� ���ҽ�Ŵ
	bool fetch(long &x, unsigned long &remain)
	{
		if (remain < sizeof(x))
			return false;
		unsigned long nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}

	bool fetch(unsigned long &x, unsigned long &remain)
	{
		if (remain < sizeof(x))
			return false;
		unsigned long nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}

	bool fetch(float &x, unsigned long &remain)
	{
		if (remain < sizeof(x))
			return false;
		unsigned long nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}

	bool fetch(double &x, unsigned long &remain)
	{
		if (remain < sizeof(x))
			return false;
		unsigned long nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}

	bool fetch(short &x, unsigned long &remain)
	{
		if (remain < sizeof(x))
			return false;
		unsigned long nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}

	bool fetch(unsigned short &x, unsigned long &remain)
	{
		if (remain < sizeof(x))
			return false;
		unsigned long nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}

	bool fetch(char &x, unsigned long &remain)
	{
		if (remain < sizeof(x))
			return false;
		unsigned long nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}

	bool fetch(unsigned char &x, unsigned long &remain)
	{
		if (remain < sizeof(x))
			return false;
		unsigned long nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}

	// Fetch for unsigned int (which is causing errors)
	bool fetch(unsigned int &x, unsigned long &remain)
	{
		if (remain < sizeof(x))
			return false;
		unsigned long nread = read(&x, sizeof(x));
		remain -= nread;
		return (nread == sizeof(x));
	}

	// Fetch for string (fixing tname, unit, dtype, dname errors)
	bool fetch(std::string &x, unsigned long &remain)
	{
		unsigned long strlen;
		if (!fetch(strlen, remain))
			return false; // First read string length

		if (strlen >= 1048576)
			return false; // Sanity check (max 1MB)

		x.resize(strlen);
		unsigned long nread = read(&x[0], strlen);
		remain -= nread;
		return (nread == strlen);
	}

	// Inside class GZReader:
	template <typename T, typename R>
	bool fetch(T &x, R &remain)
	{
		static_assert(std::is_integral<R>::value,
					  "fetch(...) second parameter must be integral");
		if (remain < sizeof(T))
			return false;

		// We always call read(...) with an unsigned long for the size:
		unsigned long nread = read(&x, sizeof(T));
		remain -= nread; // remain is your integral type
		return (nread == sizeof(T));
	}

	bool fetch_with_len(std::string &x, unsigned long &remain)
	{
		// Read 4 bytes for the length, not 'unsigned long' (which might be 8 bytes).
		uint32_t strLen32 = 0;
		if (!fetch(strLen32, remain))
			return false; // read 4 bytes

		// Check for sanity
		if (strLen32 >= 1048576)
			return false;

		// Now read that many bytes into x
		x.resize(strLen32);
		unsigned long nread = read(&x[0], strLen32);
		remain -= nread;
		return (nread == strLen32);
	}

	bool opened() const
	{
		return m_fi != 0;
	}

	bool eof() const
	{
		return gzeof(m_fi) && !fi_remain;
	}

	void rewind()
	{
		gzrewind(m_fi);
		fi_remain = 0;
		fi_ptr = fi_buf;
	}
};

class BUF : public vector<unsigned char>
{
	unsigned long pos = 0;

public:
	BUF(unsigned long len = 0) : vector<unsigned char>(len) { pos = 0; }
	void skip(unsigned long len)
	{
		pos += len;
	}
	void skip_str()
	{
		unsigned long strlen;
		if (!fetch(strlen))
			return;
		pos += strlen;
	}
	bool fetch(void *p, unsigned long len)
	{
		if (size() < pos + len)
		{
			pos = size();
			return false;
		}
		memcpy(p, &(*this)[pos], len); // �� �ܿ��� ������ ���� ����
		pos += len;
		return true;
	}
	bool fetch(unsigned char &x)
	{
		if (size() < pos + sizeof(x))
		{
			pos = size();
			return false;
		}
		memcpy(&x, &(*this)[pos], sizeof(x)); // �� �ܿ��� ������ ���� ����
		pos += sizeof(x);
		return true;
	}
	bool fetch(unsigned short &x)
	{
		if (size() < pos + sizeof(x))
		{
			pos = size();
			return false;
		}
		memcpy(&x, &(*this)[pos], sizeof(x)); // �� �ܿ��� ������ ���� ����
		pos += sizeof(x);
		return true;
	}
	bool fetch(unsigned long &x)
	{
		if (size() < pos + sizeof(x))
		{
			pos = size();
			return false;
		}
		memcpy(&x, &(*this)[pos], sizeof(x)); // �� �ܿ��� ������ ���� ����
		pos += sizeof(x);
		return true;
	}
	bool fetch(float &x)
	{
		if (size() < pos + sizeof(x))
		{
			pos = size();
			return false;
		}
		memcpy(&x, &(*this)[pos], sizeof(x)); // �� �ܿ��� ������ ���� ����
		pos += sizeof(x);
		return true;
	}
	bool fetch(double &x)
	{
		if (size() < pos + sizeof(x))
		{
			pos = size();
			return false;
		}
		memcpy(&x, &(*this)[pos], sizeof(x)); // �� �ܿ��� ������ ���� ����
		pos += sizeof(x);
		return true;
	}
	bool fetch_with_len(string &x)
	{
		unsigned long strlen;
		if (!fetch(strlen))
			return false;
		if (strlen >= 1048576)
		{ // > 1MB
			return false;
		}
		x.resize(strlen);
		if (size() < pos + strlen)
		{
			pos = size();
			return false;
		}
		memcpy(&x[0], &(*this)[pos], strlen);
		pos += strlen;
		return true;
	}
};
