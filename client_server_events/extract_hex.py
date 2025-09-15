def extract_hex_from_log_line(line):
    """Extract hex numbers from a log line containing patterns like '01 03'"""
    hex_numbers = []
    
    # Method 1: Regex approach (most reliable)
    import re
    hex_pattern = r'\b[0-9A-Fa-f]{2}\s*[0-9A-Fa-f]{2}\b'
    matches = re.findall(hex_pattern, line)
    
    for match in matches:
        # Remove spaces and split into individual hex numbers
        clean_match = match.replace(' ', '')
        for i in range(0, len(clean_match), 2):
            hex_num = clean_match[i:i+2]
            if len(hex_num) == 2:
                hex_numbers.append(hex_num.upper())
    
    return hex_numbers

test_line = " aa bb efghijk xx zz 5d cc 85 e2 ca 7d bonjour 15 0d 03 17 hello 00 ciao"
result = extract_hex_from_log_line(test_line)
print(f"Extracted hex numbers: {result}")