package aitx

type Transaction struct {
	ID          string `json:"id"`
	Timestamp   string `json:"timestamp"`
	Description string `json:"description"`
	Reverted    bool   `json:"reverted"`
}
