// Copyright AgentZet. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AgentZetTypes.h"

class IAgentZetLLMClient;
class FAgentZetConversationManager;

/**
 * Result of a conversation condensation operation.
 */
struct FAgentZetCondenseResult
{
	bool bSuccess = false;
	FString Summary;
	FString ErrorMessage;
	FString CondenseId;
	int32 NewContextTokens = 0;
};

/**
 * Options for the summarization call (mirrors Zoo-Code's SummarizeConversationOptions).
 */
struct FAgentZetSummarizeOptions
{
	FString SystemPrompt;
	FString CustomCondensingPrompt;
	bool bIsAutomaticTrigger = false;
	FString EnvironmentDetails;
	FString FoldedCodeContext;
};

/**
 * LLM-based conversation summarization (condenser).
 *
 * v3.1: Updated to match Zoo-Code's condensation:
 *   - Summarizes only messages since last summary (incremental)
 *   - Preserves <command> blocks as <system-reminder>
 *   - Supports custom condensing prompts from settings
 *   - Injects environment details for auto-condense
 */
class AGENTZETLLM_API FAgentZetContextCondenser
{
public:
	FAgentZetContextCondenser(
		TSharedPtr<IAgentZetLLMClient> InLLMClient,
		TSharedPtr<FAgentZetConversationManager> InConversationManager);

	~FAgentZetContextCondenser();

	void SummarizeConversation(
		const FAgentZetSummarizeOptions& Options,
		TFunction<void(const FAgentZetCondenseResult&)> OnComplete);

	bool IsCondensing() const { return bIsCondensing; }
	void SetLLMClient(TSharedPtr<IAgentZetLLMClient> InLLMClient) { LLMClient = InLLMClient; }

	static const FString SummaryPrompt;
	static const FString DefaultCondenseInstructions;

private:
	static TArray<FAgentZetMessage> GetMessagesSinceLastSummary(const TArray<FAgentZetMessage>& Messages);
	static FString ExtractCommandBlocks(const FAgentZetMessage& Message);
	static TArray<FAgentZetMessage> ConvertToolBlocksToText(const TArray<FAgentZetMessage>& Messages);
	void InjectSyntheticToolResults(TArray<FAgentZetMessage>& Messages);
	void ApplyCondensation(const FString& Summary, const FAgentZetCondenseResult& Result,
		const FString& CommandBlocks, const FString& FoldedCodeContext,
		bool bIsAutomaticTrigger, const FString& EnvironmentDetails);

	TSharedPtr<IAgentZetLLMClient> LLMClient;
	TSharedPtr<FAgentZetConversationManager> ConversationManager;
	bool bIsCondensing = false;

	FDelegateHandle StreamingHandle;
	FDelegateHandle MessageCompleteHandle;
	FDelegateHandle RequestCompletedHandle;
	FString AccumulatedSummary;
	TFunction<void(const FAgentZetCondenseResult&)> PendingCallback;
};
