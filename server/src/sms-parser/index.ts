export interface SmsParseResult {
  parsed: boolean;
  code?: string;
  codeType?: 'verification' | 'pickup' | 'confirmation' | 'unrecognized';
}

interface SmsRule {
  type: SmsParseResult['codeType'];
  keywords: string[];
  pattern: RegExp;
  extractIndex: number;
}

const rules: SmsRule[] = [
  {
    type: 'verification',
    keywords: ['验证码', '动态码'],
    pattern: /(验证码|动态码).*?(\d{4,8})/,
    extractIndex: 2,
  },
  {
    type: 'pickup',
    keywords: ['取件码'],
    pattern: /取件码[：: ]?([\d\-]+)/,
    extractIndex: 1,
  },
  {
    type: 'confirmation',
    keywords: ['确认码', '交易码'],
    pattern: /(确认码|交易码).*?(\d{4,8})/,
    extractIndex: 2,
  },
];

/**
 * Parse SMS content and extract codes
 */
export function parseSms(sender: string, content: string): SmsParseResult {
  for (const rule of rules) {
    // Check if any keyword is present (case-insensitive)
    const hasKeyword = rule.keywords.some((kw) => content.includes(kw));
    if (!hasKeyword) continue;

    const match = content.match(rule.pattern);
    if (match && match[rule.extractIndex]) {
      const code = match[rule.extractIndex].trim();
      return {
        parsed: true,
        code,
        codeType: rule.type,
      };
    }
  }

  // No rules matched
  return {
    parsed: false,
    codeType: 'unrecognized',
  };
}
