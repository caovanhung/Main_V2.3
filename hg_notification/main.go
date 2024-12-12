package main

import (
	"context"
	"fmt"
	"log"
	"os"

	firebase "firebase.google.com/go"
	"firebase.google.com/go/messaging"
	"google.golang.org/api/option"
)

func main() {
	// Check if enough command-line arguments are provided
	if len(os.Args) < 4 {
		fmt.Println("Usage: go run main.go <topic> <title> <body>")
		return
	}

	// Get topic, title, and body from command-line arguments
	topic := os.Args[1]
	title := os.Args[2]
	body := os.Args[3]

	// Load service account key JSON file
	opt := option.WithCredentialsFile("firebase-sdk.json")

	// Initialize the Firebase app
	ctx := context.Background()
	app, err := firebase.NewApp(ctx, nil, opt)
	if err != nil {
		log.Fatalf("Error initializing Firebase app: %v\n", err)
	}

	// Get the messaging client
	client, err := app.Messaging(ctx)
	if err != nil {
		log.Fatalf("Error getting Firebase Messaging client: %v\n", err)
	}

	// Construct the message
	message := &messaging.Message{
		Notification: &messaging.Notification{
			Title: title,
			Body:  body,
		},
		Topic: topic,
	}

	// Send the message
	response, err := client.Send(ctx, message)
	if err != nil {
		log.Fatalf("Error: %v\n", err)
	}

	// Handle the response
	fmt.Printf("Success: %v\n", response)
}
