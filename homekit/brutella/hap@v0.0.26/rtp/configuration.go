package rtp

type Configuration struct {
	Suites []SupportedCryptoSuite `tlv8:"-"`
}

func NewConfiguration(suite byte) Configuration {
	return Configuration{[]SupportedCryptoSuite{
		SupportedCryptoSuite{suite},
	}}
}

type SupportedCryptoSuite struct {
	Type byte `tlv8:"2"`
}

const (
	CryptoSuite_AES_CM_128_HMAC_SHA1_80 byte = 0
	CryptoSuite_AES_256_CM_HMAC_SHA1_80      = 1
	CryptoSuiteNone                          = 2
)
