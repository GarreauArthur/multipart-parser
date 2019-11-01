#ifndef _MULTIPART_PARSER_H_
#define _MULTIPART_PARSER_H_

#include <sys/types.h>
#include <string>
#include <stdexcept>
#include <cstring>
#include <iostream>
class MultipartParser {
public:
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
	
	const char *boundaryData;
	size_t boundarySize;
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
	
	void indexBoundary() {
		const char *current;
		const char *end = boundaryData + boundarySize;
		
		memset(boundaryIndex, 0, sizeof(boundaryIndex));
		
		for (current = boundaryData; current < end; current++) {
			boundaryIndex[(unsigned char) *current] = true;
		}
	}
	
	void callback(Callback cb, std::string_view buffer = NULL, size_t start = UNMARKED,
		size_t end = UNMARKED, bool allowEmpty = false)
	{
		std::cout << "BREAKPOINT 2.3.callback.1" << std::endl;
		if (start != UNMARKED && start == end && !allowEmpty) {
		std::cout << "BREAKPOINT 2.3.callback.2" << std::endl;
			return;
		}
		if (cb != NULL) {
			std::cout << "BREAKPOINT 2.3.callback.3" << std::endl;
			cb(buffer, start, end, userData);
		}
	}
	
	void dataCallback(Callback cb, size_t &mark, std::string_view buffer, size_t i, size_t bufferLen,
		bool clear, bool allowEmpty = false)
	{
		std::cout << "BREAKPOINT 2.3.1" << std::endl;
		if (mark == UNMARKED) {
			return;
		}
		
		std::cout << "BREAKPOINT 2.3.2" << std::endl;
		if (!clear) {
			std::cout << "BREAKPOINT 2.3.3" << std::endl;
			callback(cb, buffer, mark, bufferLen, allowEmpty);
			mark = 0;
		} else {
			std::cout << "BREAKPOINT 2.3.4" << std::endl;
			callback(cb, buffer, mark, i, allowEmpty);
			mark = UNMARKED;
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
		prevIndex = index;
		
		if (index == 0) {
			// boyer-moore derived algorithm to safely skip non-boundary data
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
			if (boundary[index] == c) {
				if (index == 0) {
					dataCallback(onPartData, partDataMark, buffer, i, len, true);
				}
				index++;
			} else {
				index = 0;
			}
		} else if (index == boundarySize) {
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
			if (c == CR) {
				index++;
			} else {
				index = 0;
			}
		} else if (index - boundarySize == 3) {
			index = 0;
			if (c == LF) {
				callback(onPartEnd);
				callback(onEnd);
				state = END;
				return;
			}
		}
		
		if (index > 0) {
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
			lookbehind[index - 1] = c;
		} else if (prevIndex > 0) {
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
	std::string boundary;
	Callback onPartBegin;
	Callback onHeaderField;
	Callback onHeaderValue;
	Callback onHeaderEnd;
	Callback onHeadersEnd;
	Callback onPartData;
	Callback onPartEnd;
	Callback onEnd;
	void *userData;
	
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
	~MultipartParser() {
		delete[] lookbehind;
	}
*/	
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
		std::cout << this->boundary << "setboundary" << std::endl;
		boundaryData = this->boundary.c_str();
		std::cout << this->boundary << "setboundary c_str" << std::endl;
		boundarySize = this->boundary.size();
		indexBoundary();
		lookbehind = new char[boundarySize + 8];
		lookbehindSize = boundarySize + 8;
		state = START;
		errorReason = "No error.";
	}
	
	size_t feed(std::string_view buffer, size_t len) {
		if (state == ERROR || len == 0) {
			return 0;
		}
		
		State state         = this->state;
		int flags           = this->flags;
		size_t prevIndex    = this->index;
		size_t index        = this->index;
		size_t boundaryEnd  = boundarySize - 1;
		size_t i;
		char c, cl;
		std::cout << boundary << "feed" << std::endl;
		for (i = 0; i < len; i++) {
			c = buffer[i];
			
			switch (state) {
			case ERROR:
				return i;
			case START:
				index = 0;
				state = START_BOUNDARY;
			case START_BOUNDARY:
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
					std::cout << "oijaeifj paoijepofgi japoeigjp aejpgoi japogiej aioejg  HERE" << std::endl;
					std::cout << int(c) << " vs " << int(boundary[index+2]) << std::endl;
					std::cout << c << " vs " << boundary[index+2] << std::endl;
					setError("Malformed. Found different boundary data than the given one.");
					return i;
				}
				index++;
				break;
			case HEADER_FIELD_START:
				std::cout << "BREAKPOINT 1" << std::endl;
				state = HEADER_FIELD;
				headerFieldMark = i;
				index = 0;
			case HEADER_FIELD:
				std::cout << "BREAKPOINT 2" << std::endl;
				if (c == CR) {
					std::cout << "BREAKPOINT 2.1" << std::endl;
					headerFieldMark = UNMARKED;
					state = HEADERS_ALMOST_DONE;
					break;
				}

				index++;
				if (c == HYPHEN) {
					std::cout << "BREAKPOINT 2.2" << std::endl;
					break;
				}

				if (c == COLON) {
					std::cout << "BREAKPOINT 2.3" << std::endl;
					if (index == 1) {
						// empty header field
						setError("Malformed first header name character.");
						return i;
					}
					dataCallback(onHeaderField, headerFieldMark, buffer, i, len, true);
					state = HEADER_VALUE_START;
					break;
				}

				std::cout << "BREAKPOINT 3" << std::endl;
				cl = lower(c);
				if (cl < 'a' || cl > 'z') {
					setError("Malformed header name.");
					return i;
				}
				break;
			case HEADER_VALUE_START:
				std::cout << "BREAKPOINT 4" << std::endl;
				if (c == SPACE) {
					break;
				}
				
				headerValueMark = i;
				state = HEADER_VALUE;
			case HEADER_VALUE:
				std::cout << "BREAKPOINT 5" << std::endl;
				if (c == CR) {
					dataCallback(onHeaderValue, headerValueMark, buffer, i, len, true, true);
					callback(onHeaderEnd);
					state = HEADER_VALUE_ALMOST_DONE;
				}
				break;
			case HEADER_VALUE_ALMOST_DONE:
				std::cout << "BREAKPOINT 6" << std::endl;
				if (c != LF) {
					setError("Malformed header value: LF expected after CR");
					return i;
				}
				
				state = HEADER_FIELD_START;
				break;
			case HEADERS_ALMOST_DONE:
				std::cout << "BREAKPOINT 7" << std::endl;
				if (c != LF) {
					setError("Malformed header ending: LF expected after CR");
					return i;
				}
				
				callback(onHeadersEnd);
				state = PART_DATA_START;
				break;
			case PART_DATA_START:
				std::cout << "BREAKPOINT 8" << std::endl;
				state = PART_DATA;
				partDataMark = i;
			case PART_DATA:
				std::cout << "BREAKPOINT 9" << std::endl;
				processPartData(prevIndex, index, buffer, len, boundaryEnd, i, c, state, flags);
				break;
			default:
				return i;
			}
		}
		
		std::cout << "BREAKPOINT 10" << std::endl;
		dataCallback(onHeaderField, headerFieldMark, buffer, i, len, false);
		std::cout << "BREAKPOINT 11" << std::endl;
		dataCallback(onHeaderValue, headerValueMark, buffer, i, len, false);
		std::cout << "BREAKPOINT 12" << std::endl;
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
