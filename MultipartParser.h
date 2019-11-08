#ifndef _MULTIPART_PARSER_H_
#define _MULTIPART_PARSER_H_

#include <sys/types.h>
#include <string>
#include <stdexcept>
#include <cstring>
#include <iostream>

#define LOG(x) std::cout << x << std::endl;
//#define LOG(x) ;

class MultipartParser {
public:
	// typedef Callback to define our callbacks
	typedef void (*Callback)(std::string_view buffer, size_t start, size_t end, void *userData);
	
private:
	static const char CR     = 13;
	static const char LF     = 10;
	static const char SPACE  = 32;
	static const char HYPHEN = 45;
	static const char COLON  = 58;
	static const size_t UNMARKED = (size_t) -1;
	
	enum State {
		ERROR,
		START,
		START_BOUNDARY,
		HEADER_FIELD_START,
		HEADER_FIELD,
		HEADER_VALUE_START,
		HEADER_VALUE,
		HEADER_VALUE_ALMOST_DONE,
		HEADERS_ALMOST_DONE,
		PART_DATA_START,
		PART_DATA,
		PART_END,
		END
	};
	
	enum Flags {
		PART_BOUNDARY = 1,
		LAST_BOUNDARY = 2
	};
	
	void resetCallbacks() {
		onPartBegin   = NULL;
		onHeaderField = NULL;
		onHeaderValue = NULL;
		onHeaderEnd   = NULL;
		onHeadersEnd  = NULL;
		onPartData    = NULL;
		onPartEnd     = NULL;
		onEnd         = NULL;
		userData      = NULL;
	}
	
	/**
	 * Used to implement the Boyer-moore algo, substring search (boundary)
	 */
	void indexBoundary() {
		const char *current;
		const char *end = boundaryData + boundarySize;
		
		memset(boundaryIndex, 0, sizeof(boundaryIndex));
		
		for (current = boundaryData; current < end; current++) {
			boundaryIndex[(unsigned char) *current] = true;
		}
	}
	
	void callback(Callback callback, std::string_view buffer = NULL, size_t start = UNMARKED,
		size_t end = UNMARKED, bool allowEmpty = false)
	{
		LOG("BREAKPOINT 2.3.callback.1")
		if (start != UNMARKED && start == end && !allowEmpty) {
		LOG("BREAKPOINT 2.3.callback.2")
			return;
		}
		if (callback != NULL) {
			LOG("BREAKPOINT 2.3.callback.3")
			callback(buffer, start, end, userData);
		}
	}
	
	void dataCallback(Callback cb, size_t &mark, std::string_view buffer, size_t i, size_t bufferLen,
		bool clear, bool allowEmpty = false)
	{
		LOG("BREAKPOINT 2.3.1")
		if (mark == UNMARKED) {
			LOG("BREAKPOINT 2.3.1.1 UNMARKED")
			return;
		}
		
		LOG("BREAKPOINT 2.3.2")
		if (!clear) {
			LOG("BREAKPOINT 2.3.3")
			callback(cb, buffer, mark, bufferLen, allowEmpty);
			mark = 0;
		} else {
			LOG("BREAKPOINT 2.3.4")
			callback(cb, buffer, mark, i, allowEmpty);
			LOG("BREAKPOINT 2.3.4.1")
			mark = UNMARKED;
			LOG("BREAKPOINT 2.3.4.2")
		}
	}
	
	char lower(char c) const {
		return c | 0x20;
	}
	
	inline bool isBoundaryChar(char c) const {
		return boundaryIndex[(unsigned char) c];
	}
	
	bool isHeaderFieldCharacter(char c) const {
		return (c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')
			|| c == HYPHEN;
	}
	
	void setError(const char *message) {
		state = ERROR;
		errorReason = message;
	}
	
	void processPartData(size_t &prevIndex, size_t &index, std::string_view buffer,
		size_t len, size_t boundaryEnd, size_t &i, char c, State &state, int &flags)
	{
		LOG("BREAK processPartData")
		prevIndex = index;
		
		if (index == 0) {
			// boyer-moore derived algorithm to safely skip non-boundary data
			LOG("BREAK ppd 1")
			while (i + boundarySize <= len) {
				if (isBoundaryChar(buffer[i + boundaryEnd])) {
					break;
				}
				
				i += boundarySize;
			}
			if (i == len) {
				return;
			}
			c = buffer[i];
		}
		
		if (index < boundarySize) {
			LOG("BREAK ppd 2")
			if (boundary[index] == c) {
				if (index == 0) {
					dataCallback(onPartData, partDataMark, buffer, i, len, true);
				}
				index++;
			} else {
				index = 0;
			}
		} else if (index == boundarySize) {
			LOG("BREAK ppd 3")
			index++;
			if (c == CR) {
				// CR = part boundary
				flags |= PART_BOUNDARY;
			} else if (c == HYPHEN) {
				// HYPHEN = end boundary
				flags |= LAST_BOUNDARY;
			} else {
				index = 0;
			}
		} else if (index - 1 == boundarySize) {
			LOG("BREAK ppd 4")
			std::cout << "BREAK POINT " << std::endl;
			if (flags & PART_BOUNDARY) {
				index = 0;
				if (c == LF) {
					// unset the PART_BOUNDARY flag
					flags &= ~PART_BOUNDARY;
					callback(onPartEnd);
					callback(onPartBegin);
					state = HEADER_FIELD_START;
					return;
				}
			} else if (flags & LAST_BOUNDARY) {
				if (c == HYPHEN) {
                    callback(onPartEnd);
                    callback(onEnd);
                    state = END;
				} else {
					index = 0;
				}
			} else {
				index = 0;
			}
		} else if (index - 2 == boundarySize) {
			LOG("BREAK ppd 5")
			if (c == CR) {
				index++;
			} else {
				index = 0;
			}
		} else if (index - boundarySize == 3) {
			LOG("BREAK ppd 6")
			index = 0;
			if (c == LF) {
				callback(onPartEnd);
				callback(onEnd);
				state = END;
				return;
			}
		}
		
		if (index > 0) {
			LOG("BREAK ppd 7")
			// when matching a possible boundary, keep a lookbehind reference
			// in case it turns out to be a false lead
			if (index - 1 >= lookbehindSize) {
				setError("Parser bug: index overflows lookbehind buffer. "
					"Please send bug report with input file attached.");
				throw std::out_of_range("index overflows lookbehind buffer");
			} else if (index - 1 < 0) {
				setError("Parser bug: index underflows lookbehind buffer. "
					"Please send bug report with input file attached.");
				throw std::out_of_range("index underflows lookbehind buffer");
			}
			LOG("BREAK ppd 7.1")
			lookbehind[index - 1] = c;
			LOG("BREAK ppd 7.2")
		} else if (prevIndex > 0) {
			LOG("BREAK ppd 8")
			// if our boundary turned out to be rubbish, the captured lookbehind
			// belongs to partData
			callback(onPartData, lookbehind, 0, prevIndex);
			prevIndex = 0;
			partDataMark = i;
			
			// reconsider the current character even so it interrupted the sequence
			// it could be the beginning of a new sequence
			i--;
		}
	}
	
public:


	// Callbacks
	Callback onPartBegin;
	Callback onHeaderField;
	Callback onHeaderValue;
	Callback onHeaderEnd;
	Callback onHeadersEnd;
	Callback onPartData;
	Callback onPartEnd;
	Callback onEnd;

	// ??? shouldn't it be Callback instead of void*
	void *userData;

	std::string boundary;

	// c string of boundary, why ? idk
	const char *boundaryData;
	// ... and its size
	size_t boundarySize;

	// useful for finding the boundary (using the Boyer-Moore algo)
	bool boundaryIndex[256];

	// when matching a possible boundary, keep a lookbehind reference
	// in case it turns out to be a false lead
	char *lookbehind;
	size_t lookbehindSize;

	State state; // to remember what data the parser is parsing (header, data...)
	int flags; // to know if part or last boundary

	size_t index; // position of current character on the line ?

	size_t headerFieldMark; // start of header field name
	size_t headerValueMark; // start of header value
	size_t partDataMark; // start of part data

	const char *errorReason;
	
	
	MultipartParser() {
		lookbehind = NULL;
		resetCallbacks();
		reset();
	}
	
	MultipartParser(const std::string &boundary) {
		lookbehind = NULL;
		resetCallbacks();
		setBoundary(boundary);
	}

	// move assignement operator
	MultipartParser& operator=(MultipartParser&& old) {

		if ( this == &old )
			return *this;

		onPartBegin = old.onPartBegin;
		onHeaderField = old.onHeaderField;
		onHeaderValue = old.onHeaderValue;
		onHeaderEnd = old.onHeaderEnd;
		onHeadersEnd = old.onHeadersEnd;
		onPartData = old.onPartData;
		onPartEnd = old.onPartEnd;
		onEnd = old.onEnd;
	  	boundary = old.boundary;
		boundarySize = old.boundarySize;
		
		lookbehindSize = old.lookbehindSize;
		state = old.state;
		flags = old.flags;
		index = old.index;
		headerFieldMark = old.headerFieldMark;
		headerValueMark = old.headerValueMark;
		partDataMark = old.partDataMark;

		std::copy_n(old.boundaryIndex, 256, boundaryIndex);
		errorReason = std::move(old.errorReason);
		boundaryData = std::move(old.boundaryData);
		lookbehind = old.lookbehind;

		// invalidate old
		//old.boundary = NULL;
		old.onPartBegin = nullptr;
		old.onHeaderField = nullptr;
		old.onHeaderValue = nullptr;
		old.onHeaderEnd = nullptr;
		old.onHeadersEnd = nullptr;
		old.onPartData = nullptr;
		old.onPartEnd = nullptr;
		old.onEnd = nullptr;
		old.userData = nullptr;
		old.boundaryData = nullptr;
		old.boundarySize = NULL;
		//old.boundaryIndex = nullptr;
		old.lookbehind = nullptr;
		old.lookbehindSize = NULL;
		//old.state = NULL;
		old.flags = NULL;
		old.index = NULL;
		old.headerFieldMark = NULL;
		old.headerValueMark = NULL;
		old.partDataMark = NULL;
		old.errorReason = nullptr;
		return *this;

	}
	// TODO move constructor

	MultipartParser(MultipartParser&& old) {

		LOG("move constructor")
		onPartBegin = old.onPartBegin;
		onHeaderField = old.onHeaderField;
		onHeaderValue = old.onHeaderValue;
		onHeaderEnd = old.onHeaderEnd;
		onHeadersEnd = old.onHeadersEnd;
		onPartData = old.onPartData;
		onPartEnd = old.onPartEnd;
		onEnd = old.onEnd;
	        boundary = old.boundary;
		boundarySize = old.boundarySize;
		
		lookbehindSize = old.lookbehindSize;
		state = old.state;
		flags = old.flags;
		index = old.index;
		headerFieldMark = old.headerFieldMark;
		headerValueMark = old.headerValueMark;
		partDataMark = old.partDataMark;

		std::copy_n(old.boundaryIndex, 256, boundaryIndex);
		errorReason = std::move(old.errorReason);
		boundaryData = std::move(old.boundaryData);
		lookbehind = old.lookbehind;

		// invalidate old
		//old.boundary = NULL;
		old.onPartBegin = nullptr;
		old.onHeaderField = nullptr;
		old.onHeaderValue = nullptr;
		old.onHeaderEnd = nullptr;
		old.onHeadersEnd = nullptr;
		old.onPartData = nullptr;
		old.onPartEnd = nullptr;
		old.onEnd = nullptr;
		old.userData = nullptr;
		old.boundaryData = nullptr;
		old.boundarySize = NULL;
		//old.boundaryIndex = nullptr;
		old.lookbehind = nullptr;
		old.lookbehindSize = NULL;
		//old.state = NULL;
		old.flags = NULL;
		old.index = NULL;
		old.headerFieldMark = NULL;
		old.headerValueMark = NULL;
		old.partDataMark = NULL;
		old.errorReason = nullptr;

	}
/* 	
	MultipartParser(MultipartParser const& mp) {
		if ( mp.getlookbehind != NULL ) {
			lookbehind = new char[mp.get]
		}
	boundaryData;
	boundarySize;
	bool boundaryIndex[256];
	char *lookbehind;
	size_t lookbehindSize;
	State state;
	int flags;
	size_t index;
	size_t headerFieldMark;
	size_t headerValueMark;
	size_t partDataMark;
	const char *errorReason;
	}
	*/
	~MultipartParser() {
		LOG("DESTROYED")
		if ( lookbehind != nullptr ) {
			delete[] lookbehind;
			lookbehind = nullptr;
		}
	}
	
	void reset() {
		delete[] lookbehind;
		state = ERROR;
		boundary.clear();
		boundaryData = boundary.c_str();
		boundarySize = 0;
		lookbehind = NULL;
		lookbehindSize = 0;
		flags = 0;
		index = 0;
		headerFieldMark = UNMARKED;
		headerValueMark = UNMARKED;
		partDataMark    = UNMARKED;
		errorReason     = "Parser uninitialized.";
	}
	
	void setBoundary(const std::string &boundary) {
		reset();
		//this->boundary = boundary;
		this->boundary = "\r\n--" + boundary;
		LOG(this->boundary << "setboundary")
		boundaryData = this->boundary.c_str();
		LOG(this->boundary << "setboundary c_str")
		boundarySize = this->boundary.size();
		indexBoundary();
		lookbehind = new char[boundarySize + 8];
		lookbehindSize = boundarySize + 8;
		state = START;
		errorReason = "No error.";
	}
	
	/** Process a small part (buffer) of the body of the request
	 * @param buffer part of the HTTP multi-form body
	 * @param len the length of the buffer
	*/
	size_t feed(std::string_view buffer, size_t len) {

		if (state == ERROR || len == 0) {
			return 0;
		}

		State state         = this->state;
		int flags           = this->flags;
		size_t prevIndex    = this->index;
		size_t index        = this->index;
		size_t boundaryEnd  = boundarySize;
		size_t i;
		char c, cl;
		LOG(boundary << "feed")
		// go through each char of in the buffer
		for (i = 0; i < len; i++) {
			c = buffer[i];
			
			switch (state) {

			case ERROR:
				return i;

			case START:
				index = 0;
				state = START_BOUNDARY;

			case START_BOUNDARY:

				// this->boundary has 2 more character (at the beginning CR LF)
				// than what we are currently reading, so the index of the last
				// char of the part boundary is not size-1 but size-3, and the
				// char before the last: size-4
				if (index == boundarySize - 4) {
					if (c != CR) {
						setError("Malformed. Expected CR after boundary.");
						return i;
					}
					index++;
					break;
				} else if (index == boundarySize - 3) {
					if (c != LF) {
						setError("Malformed. Expected LF after boundary CR.");
						return i;
					}
					index = 0;
					callback(onPartBegin);
					state = HEADER_FIELD_START;
					break;
				}
				if (c != boundary[index + 2]) {
					LOG("oijaeifj paoijepofgi japoeigjp aejpgoi japogiej aioejg  HERE")
					LOG(int(c) << " vs " << int(boundary[index+2]))
					LOG(c << " vs " << boundary[index+2])
					setError("Malformed. Found different boundary data than the given one.");
					return i;
				}
				index++;
				break;
			case HEADER_FIELD_START:
				LOG("BREAKPOINT 1")
				state = HEADER_FIELD;
				headerFieldMark = i;
				index = 0;
			case HEADER_FIELD:
				LOG("BREAKPOINT 2")
				if (c == CR) {
					LOG("BREAKPOINT 2.1")
					headerFieldMark = UNMARKED;
					state = HEADERS_ALMOST_DONE;
					break;
				}

				index++;
				if (c == HYPHEN) {
					LOG("BREAKPOINT 2.2")
					break;
				}

				if (c == COLON) {
					LOG("BREAKPOINT 2.3")
					if (index == 1) {
						// empty header field
						setError("Malformed first header name character.");
						return i;
					}
					dataCallback(onHeaderField, headerFieldMark, buffer, i, len, true);
					state = HEADER_VALUE_START;
					break;
				}

				LOG("BREAKPOINT 3")
				cl = lower(c);
				if (cl < 'a' || cl > 'z') {
					setError("Malformed header name.");;
					return i;
				}
				break;
			case HEADER_VALUE_START:
				LOG("BREAKPOINT 4")
				if (c == SPACE) {
					break;
				}
				
				headerValueMark = i; // mark start of header value in the buffer
				state = HEADER_VALUE;
			case HEADER_VALUE:
				LOG("BREAKPOINT 5")
				if (c == CR) {
					//             callback   , start          , buffer,  end  , clean, allowEmpty
					dataCallback(onHeaderValue, headerValueMark, buffer, i, len, true, true);
					callback(onHeaderEnd);
					state = HEADER_VALUE_ALMOST_DONE;
				}
				break;
			case HEADER_VALUE_ALMOST_DONE:
				LOG("BREAKPOINT 6")
				if (c != LF) {
					setError("Malformed header value: LF expected after CR");
					return i;
				}
				
				state = HEADER_FIELD_START;
				break;
			case HEADERS_ALMOST_DONE:
				LOG("BREAKPOINT 7")
				if (c != LF) {
					setError("Malformed header ending: LF expected after CR");
					return i;
				}
				
				callback(onHeadersEnd);
				state = PART_DATA_START;
				break;
			case PART_DATA_START:
				LOG("BREAKPOINT 8")
				state = PART_DATA;
				partDataMark = i;
			case PART_DATA:
				LOG("BREAKPOINT 9")
				// part data requires more processing
				// will modify i, index, prevIndex, state and flags
				processPartData(prevIndex, index, buffer, len, boundaryEnd, i, c, state, flags);
				break;
			default:
				return i;
			}
		}
		
		LOG("BREAKPOINT 10")
		dataCallback(onHeaderField, headerFieldMark, buffer, i, len, false);
		LOG("BREAKPOINT 11")
		dataCallback(onHeaderValue, headerValueMark, buffer, i, len, false);
		LOG("BREAKPOINT 12")
		dataCallback(onPartData, partDataMark, buffer, i, len, false);
		
		this->index = index;
		this->state = state;
		this->flags = flags;
		
		return len;
	}
	
	bool succeeded() const {
		return state == END;
	}
	
	bool hasError() const {
		return state == ERROR;
	}
	
	bool stopped() const {
		return state == ERROR || state == END;
	}
	
	const char *getErrorMessage() const {
		return errorReason;
	}
};


#endif /* _MULTIPART_PARSER_H_ */
