#ifndef _MULTIPART_PARSER_H_
#define _MULTIPART_PARSER_H_

#include <sys/types.h>
#include <string>
#include <stdexcept>

class MultipartParser {
public:
	typedef void (*Callback)(const char *buffer, size_t start, size_t end, void *userData);
	
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
	
	std::string boundary;
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
		onHeaderDone  = NULL;
		onHeadersDone = NULL;
		onPartData    = NULL;
		onPartEnd     = NULL;
		onEnd         = NULL;
		userData      = NULL;
	}
	
	void callback(Callback cb, const char *buffer = NULL, size_t start = UNMARKED,
		size_t end = UNMARKED, bool allowEmpty = false)
	{
		if (start != UNMARKED && start == end && !allowEmpty) {
			return;
		}
		if (cb != NULL) {
			cb(buffer, start, end, userData);
		}
	}
	
	void dataCallback(Callback cb, size_t &mark, const char *buffer, size_t i, size_t bufferLen,
		bool clear, bool allowEmpty = false)
	{
		if (mark == UNMARKED) {
			return;
		}
		
		if (!clear) {
			callback(cb, buffer, mark, bufferLen, allowEmpty);
			mark = 0;
		} else {
			callback(cb, buffer, mark, i, allowEmpty);
			mark = UNMARKED;
		}
	}
	
	char lower(char c) const {
		return c | 0x20;
	}
	
	bool isBoundaryChar(char c) const {
		const char *current = boundary.c_str();
		const char *end = current + boundary.size();
		
		while (current < end) {
			if (*current == c) {
				return true;
			}
			current++;
		}
		return false;
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
	
public:
	Callback onPartBegin;
	Callback onHeaderField;
	Callback onHeaderValue;
	Callback onHeaderDone;
	Callback onHeadersDone;
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
	
	~MultipartParser() {
		delete[] lookbehind;
	}
	
	void reset() {
		delete[] lookbehind;
		state = ERROR;
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
		this->boundary = "\r\n--" + boundary;
		lookbehind = new char[this->boundary.size() + 8];
		lookbehindSize = this->boundary.size() + 8;
		state = START;
		errorReason = "No error.";
	}
	
	size_t feed(const char *buffer, size_t len) {
		if (state == ERROR || len == 0) {
			return 0;
		}
		
		State state         = this->state;
		int flags           = this->flags;
		size_t prevIndex    = this->index;
		size_t index        = this->index;
		size_t boundarySize = boundary.size();
		size_t boundaryEnd  = boundarySize - 1;
		size_t i;
		char c, cl;
		
		for (i = 0; i < len; i++) {
			c = buffer[i];
			
			switch (state) {
			case ERROR:
				return i;
			case START:
				index = 0;
				state = START_BOUNDARY;
			case START_BOUNDARY:
				if (index == boundarySize - 2) {
					if (c != CR) {
						setError("Malformed. Expected CR after boundary.");
						return i;
					}
					index++;
					break;
				} else if (index - 1 == boundarySize - 2) {
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
					return i;
				}
				index++;
				break;
			case HEADER_FIELD_START:
				state = HEADER_FIELD;
				headerFieldMark = i;
				index = 0;
			case HEADER_FIELD:
				if (c == CR) {
					headerFieldMark = UNMARKED;
					state = HEADERS_ALMOST_DONE;
					break;
				}

				index++;
				if (c == HYPHEN) {
					break;
				}

				if (c == COLON) {
					if (index == 1) {
						// empty header field
						setError("Malformed first header name character.");
						return i;
					}
					dataCallback(onHeaderField, headerFieldMark, buffer, i, len, true);
					state = HEADER_VALUE_START;
					break;
				}

				cl = lower(c);
				if (cl < 'a' || cl > 'z') {
					setError("Malformed header name.");
					return i;
				}
				break;
			case HEADER_VALUE_START:
				if (c == SPACE) {
					break;
				}
				
				headerValueMark = i;
				state = HEADER_VALUE;
			case HEADER_VALUE:
				if (c == CR) {
					dataCallback(onHeaderValue, headerValueMark, buffer, i, len, true, true);
					state = HEADER_VALUE_ALMOST_DONE;
				}
				break;
			case HEADER_VALUE_ALMOST_DONE:
				if (c != LF) {
					setError("Malformed header value: LF expected after CR");
					return i;
				}
				
				callback(onHeaderDone);
				state = HEADER_FIELD_START;
				break;
			case HEADERS_ALMOST_DONE:
				if (c != LF) {
					setError("Malformed header ending: LF expected after CR");
					return i;
				}
				
				callback(onHeadersDone);
				state = PART_DATA_START;
				break;
			case PART_DATA_START:
				state = PART_DATA;
				partDataMark = i;
			case PART_DATA:
				prevIndex = index;
				
				if (index == 0) {
					// boyer-moore derrived algorithm to safely skip non-boundary data
					while (i + boundary.size() <= len) {
						if (isBoundaryChar(buffer[i + boundaryEnd])) {
							break;
						}
						
						i += boundary.size();
					}
					c = buffer[i];
				}
				
				if (index < boundary.size()) {
					if (boundary[index] == c) {
						if (index == 0) {
							dataCallback(onPartData, partDataMark, buffer, i, len, true);
						}
						index++;
					} else {
						index = 0;
					}
				} else if (index == boundary.size()) {
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
				} else if (index - 1 == boundary.size()) {
					if (flags & PART_BOUNDARY) {
						index = 0;
						if (c == LF) {
							// unset the PART_BOUNDARY flag
							flags &= ~PART_BOUNDARY;
							callback(onPartEnd);
							callback(onPartBegin);
							state = HEADER_FIELD_START;
							break;
						}
					} else if (flags & LAST_BOUNDARY) {
						if (c == HYPHEN) {
							index++;
						} else {
							index = 0;
						}
					} else {
						index = 0;
					}
				} else if (index - 2 == boundary.size()) {
					if (c == CR) {
						index++;
					} else {
						index = 0;
					}
				} else if (index - boundary.size() == 3) {
					index = 0;
					if (c == LF) {
						callback(onPartEnd);
						callback(onEnd);
						state = END;
						break;
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

				break;
			default:
				return i;
			}
		}
		
		dataCallback(onHeaderField, headerFieldMark, buffer, i, len, false);
		dataCallback(onHeaderValue, headerValueMark, buffer, i, len, false);
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