// Copyright AgentZet. All Rights Reserved.

#include "AgentZetContextCondenser.h"
#include "AgentZetInterfaces.h"
#include "AgentZetConversationManager.h"
#include "AgentZetTokenCounter.h"
#include "AgentZetCoreModule.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Guid.h"

// ============================================================================
// Summary Prompt (ported from Roo Code condense/index.ts)
// ============================================================================

const FString FAgentZetContextCondenser::SummaryPrompt =
	TEXT("You are a helpful AI assistant tasked with summarizing conversations.\n\n")
	TEXT("CRITICAL: This is a summarization-only request. DO NOT call any tools or functions.\n")
	TEXT("Your ONLY task is to analyze the conversation and produce a text summary.\n")
	TEXT("Respond with text only - no tool calls will be processed.\n\n")
	TEXT("CRITICAL: This summarization request is a SYSTEM OPERATION, not a user message.\n")
	TEXT("When analyzing \"user requests\" and \"user intent\", completely EXCLUDE this summarization message.\n")
	TEXT("The \"most recent user request\" and \"next step\" must be based on what the user was doing BEFORE ")
	TEXT("this system message appeared.\n")
	TEXT("The goal is for work to continue seamlessly after condensation - as if it never happened.");

// ============================================================================
// Default Condense Instructions (ported from Zoo-Code supportPrompt.default.CONDENSE)
// Rich 9-section template with <analysis> wrapper producing higher quality summaries.
// ============================================================================

const FString FAgentZetContextCondenser::DefaultCondenseInstructions =
	TEXT("CRITICAL: This summarization request is a SYSTEM OPERATION, not a user message.\n")
	TEXT("When analyzing \"user requests\" and \"user intent\", completely EXCLUDE this summarization message.\n")
	TEXT("The \"most recent user request\" and \"Optional Next Step\" must be based on what the user was doing BEFORE this system message appeared.\n")
	TEXT("The goal is for work to continue seamlessly after condensation - as if it never happened.\n\n")

	TEXT("Your task is to create a detailed summary of the conversation so far, paying close attention to the user's explicit requests and your previous actions.\n")
	TEXT("This summary should be thorough in capturing technical details, code patterns, and architectural decisions that would be essential for continuing development work without losing context.\n\n")

	TEXT("Before providing your final summary, wrap your analysis in <analysis> tags to organize your thoughts and ensure you've covered all necessary points. In your analysis process:\n\n")

	TEXT("1. Chronologically analyze each message and section of the conversation. For each section thoroughly identify:\n")
	TEXT("   - The user's explicit requests and intents\n")
	TEXT("   - Your approach to addressing the user's requests\n")
	TEXT("   - Key decisions, technical concepts and code patterns\n")
	TEXT("   - Specific details like file names, full code snippets, function signatures, file edits\n")
	TEXT("   - Errors that you ran into and how you fixed them\n")
	TEXT("   - Pay special attention to specific user feedback that you received, especially if the user told you to do something differently.\n")
	TEXT("2. Double-check for technical accuracy and completeness, addressing each required element thoroughly.\n\n")

	TEXT("Your summary should include the following sections:\n\n")

	TEXT("1. Primary Request and Intent: Capture all of the user's explicit requests and intents in detail\n")
	TEXT("2. Key Technical Concepts: List all important technical concepts, technologies, and frameworks discussed.\n")
	TEXT("3. Files and Code Sections: Enumerate specific files and code sections examined, modified, or created. Pay special attention to the most recent messages and include full code snippets where applicable and include a summary of why this file read or edit is important.\n")
	TEXT("4. Errors and fixes: List all errors that you ran into, and how you fixed them. Pay special attention to specific user feedback that you received, especially if the user told you to do something differently.\n")
	TEXT("5. Problem Solving: Document problems solved and any ongoing troubleshooting efforts.\n")
	TEXT("6. All user messages: List ALL user messages that are not tool results. These are critical for understanding the users' feedback and changing intent.\n")
	TEXT("7. Pending Tasks: Outline any pending tasks that you have explicitly been asked to work on.\n")
	TEXT("8. Current Work: Describe in detail precisely what was being worked on immediately before this summary request, paying special attention to the most recent messages from both user and assistant. Include file names and code snippets where applicable.\n")
	TEXT("9. Optional Next Step: List the next step that you will take that is related to the most recent work you were doing. IMPORTANT: ensure that this step is DIRECTLY in line with the user's most recent explicit requests, and the task you were working on immediately before this summary request. If your last task was concluded, then only list next steps if they are explicitly in line with the users request. Do not start on tangential requests or really old requests that were already completed without confirming with the user first.\n\n")

	TEXT("If there is a next step, include direct quotes from the most recent conversation showing exactly what task you were working on and where you left off. This should be verbatim to ensure there's no drift in task interpretation.\n\n")

	TEXT("Here's an example of how your output should be structured:\n\n")

	TEXT("<example>\n")
	TEXT("<analysis>\n")
	TEXT("[Your thought process, ensuring all points are covered thoroughly and accurately]\n")
	TEXT("</analysis>\n\n")
	TEXT("<summary>\n")
	TEXT("1. Primary Request and Intent:\n")
	TEXT("   [Detailed description]\n\n")
	TEXT("2. Key Technical Concepts:\n")
	TEXT("   - [Concept 1]\n")
	TEXT("   - [Concept 2]\n")
	TEXT("   - [...]\n\n")
	TEXT("3. Files and Code Sections:\n")
	TEXT("   - [File Name 1]\n")
	TEXT("      - [Summary of why this file is important]\n")
	TEXT("      - [Summary of the changes made to this file, if any]\n")
	TEXT("      - [Important Code Snippet]\n")
	TEXT("   - [File Name 2]\n")
	TEXT("      - [Important Code Snippet]\n")
	TEXT("   - [...]\n\n")
	TEXT("4. Errors and fixes:\n")
	TEXT("   - [Detailed description of error 1]:\n")
	TEXT("      - [How you fixed the error]\n")
	TEXT("      - [User feedback on the error if any]\n")
	TEXT("   - [...]\n\n")
	TEXT("5. Problem Solving:\n")
	TEXT("   [Description of solved problems and ongoing troubleshooting]\n\n")
	TEXT("6. All user messages:\n")
	TEXT("   - [Detailed non tool use user message]\n")
	TEXT("   - [...]\n\n")
	TEXT("7. Pending Tasks:\n")
	TEXT("   - [Task 1]\n")
	TEXT("   - [Task 2]\n")
	TEXT("   - [...]\n\n")
	TEXT("8. Current Work:\n")
	TEXT("   [Precise description of current work]\n\n")
	TEXT("9. Optional Next Step:\n")
	TEXT("   [Optional Next step to take]\n\n")
	TEXT("</summary>\n")
	TEXT("</example>\n\n")

	TEXT("Please provide your summary based on the conversation so far, following this structure and ensuring precision and thoroughness in your response.\n\n")

	TEXT("Note: Any <command> blocks from the original task will be automatically appended to your summary wrapped in <system-reminder> tags. You do not need to include them in your summary text.\n\n")

	TEXT("There may be additional summarization instructions provided in the included context. If so, remember to follow these instructions when creating the above summary.");

// ============================================================================
// Constructor / Destructor
// ============================================================================

FAgentZetContextCondenser::FAgentZetContextCondenser(
	TSharedPtr<IAgentZetLLMClient> InLLMClient,
	TSharedPtr<FAgentZetConversationManager> InConversationManager)
	: LLMClient(InLLMClient)
	, ConversationManager(InConversationManager)
	, bIsCondensing(false)
{
}

FAgentZetContextCondenser::~FAgentZetContextCondenser()
{
	if (LLMClient.IsValid())
	{
		if (StreamingHandle.IsValid())
			LLMClient->OnStreamingText().Remove(StreamingHandle);
		if (MessageCompleteHandle.IsValid())
			LLMClient->OnMessageComplete().Remove(MessageCompleteHandle);
		if (RequestCompletedHandle.IsValid())
			LLMClient->OnRequestCompleted().Remove(RequestCompletedHandle);
	}
}

// ============================================================================
// GetMessagesSinceLastSummary (incremental summarization)
// ============================================================================

TArray<FAgentZetMessage> FAgentZetContextCondenser::GetMessagesSinceLastSummary(const TArray<FAgentZetMessage>& Messages)
{
	int32 LastSummaryIdx = -1;
	for (int32 i = Messages.Num() - 1; i >= 0; --i)
	{
		if (Messages[i].bIsSummary)
		{
			LastSummaryIdx = i;
			break;
		}
	}

	if (LastSummaryIdx == -1)
	{
		return Messages;
	}

	TArray<FAgentZetMessage> SinceLastSummary;
	for (int32 i = LastSummaryIdx; i < Messages.Num(); ++i)
	{
		SinceLastSummary.Add(Messages[i]);
	}

	return SinceLastSummary;
}

// ============================================================================
// ExtractCommandBlocks
// ============================================================================

FString FAgentZetContextCondenser::ExtractCommandBlocks(const FAgentZetMessage& Message)
{
	FString Text;

	if (Message.Role == EAgentZetMessageRole::Assistant && !Message.ContentBlocksJson.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> ContentBlocks;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message.ContentBlocksJson);
		if (FJsonSerializer::Deserialize(Reader, ContentBlocks))
		{
			for (const TSharedPtr<FJsonValue>& Block : ContentBlocks)
			{
				const TSharedPtr<FJsonObject>* BlockObj = nullptr;
				if (!Block->TryGetObject(BlockObj)) continue;
				FString BlockType;
				(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);
				if (BlockType == TEXT("text"))
				{
					FString BlockText;
					(*BlockObj)->TryGetStringField(TEXT("text"), BlockText);
					Text += BlockText;
				}
			}
		}
	}
	else
	{
		Text = Message.Content;
	}

	if (Text.IsEmpty()) return FString();

	FString Result;
	const FString StartTag = TEXT("<command");
	const FString EndTag = TEXT("</command>");

	int32 SearchStart = 0;
	while (true)
	{
		int32 StartIdx = Text.Find(StartTag, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchStart);
		if (StartIdx == INDEX_NONE) break;

		int32 TagEnd = Text.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, StartIdx);
		if (TagEnd == INDEX_NONE) break;

		int32 EndIdx = Text.Find(EndTag, ESearchCase::IgnoreCase, ESearchDir::FromStart, TagEnd);
		if (EndIdx == INDEX_NONE) break;

		int32 BlockEnd = EndIdx + EndTag.Len();
		FString CommandBlock = Text.Mid(StartIdx, BlockEnd - StartIdx);

		if (!Result.IsEmpty()) Result += TEXT("\n");
		Result += CommandBlock;

		SearchStart = BlockEnd;
	}

	return Result;
}

// ============================================================================
// SummarizeConversation (v3.1 — matches Zoo-Code flow)
// ============================================================================

void FAgentZetContextCondenser::SummarizeConversation(
	const FAgentZetSummarizeOptions& Options,
	TFunction<void(const FAgentZetCondenseResult&)> OnComplete)
{
	if (bIsCondensing)
	{
		UE_LOG(LogAgentZet, Warning, TEXT("ContextCondenser: Already condensing. Ignoring request."));
		FAgentZetCondenseResult FailResult;
		FailResult.bSuccess = false;
		FailResult.ErrorMessage = TEXT("Condensation already in progress.");
		OnComplete(FailResult);
		return;
	}

	if (!LLMClient.IsValid() || !ConversationManager.IsValid())
	{
		UE_LOG(LogAgentZet, Error, TEXT("ContextCondenser: Invalid LLM client or conversation manager."));
		FAgentZetCondenseResult FailResult;
		FailResult.bSuccess = false;
		FailResult.ErrorMessage = TEXT("Invalid client or conversation manager.");
		OnComplete(FailResult);
		return;
	}

	const TArray<FAgentZetMessage>& FullHistory = ConversationManager->GetHistory();

	// Get messages since last summary (incremental summarization)
	TArray<FAgentZetMessage> MessagesToSummarize = GetMessagesSinceLastSummary(FullHistory);

	if (MessagesToSummarize.Num() <= 1)
	{
		FAgentZetCondenseResult FailResult;
		FailResult.bSuccess = false;
		FailResult.ErrorMessage = MessagesToSummarize.Num() <= 1
			? TEXT("Not enough messages to condense.")
			: TEXT("Context was recently condensed; skipping this attempt.");
		OnComplete(FailResult);
		return;
	}

	// Check for recent summary in messages to summarize
	bool bRecentSummaryExists = false;
	for (const FAgentZetMessage& Msg : MessagesToSummarize)
	{
		if (Msg.bIsSummary)
		{
			bRecentSummaryExists = true;
			break;
		}
	}
	if (bRecentSummaryExists && MessagesToSummarize.Num() <= 2)
	{
		FAgentZetCondenseResult FailResult;
		FailResult.bSuccess = false;
		FailResult.ErrorMessage = TEXT("Context was recently condensed; skipping this attempt.");
		OnComplete(FailResult);
		return;
	}

	// Extract <command> blocks from the first message (original task)
	FString CommandBlocks;
	if (FullHistory.Num() > 0)
	{
		CommandBlocks = ExtractCommandBlocks(FullHistory[0]);
	}

	bIsCondensing = true;
	AccumulatedSummary.Empty();
	PendingCallback = OnComplete;

	// Prepare messages: convert tool blocks to text + inject synthetic tool results
	TArray<FAgentZetMessage> PreparedMessages = ConvertToolBlocksToText(MessagesToSummarize);
	InjectSyntheticToolResults(PreparedMessages);

	// Choose condense instructions: custom prompt if provided, otherwise rich default
	FString InstructionsContent;
	if (!Options.CustomCondensingPrompt.IsEmpty())
	{
		InstructionsContent = Options.CustomCondensingPrompt;
	}
	else
	{
		InstructionsContent = DefaultCondenseInstructions;
	}

	// Inject folded code context as a system-reminder in the instructions
	if (!Options.FoldedCodeContext.IsEmpty())
	{
		InstructionsContent += TEXT("\n\n<system-reminder>\n## Code Structure Context\n");
		InstructionsContent += TEXT("The following code structure was seen during this task. Include awareness of these files in your summary:\n\n");
		InstructionsContent += Options.FoldedCodeContext;
		InstructionsContent += TEXT("\n</system-reminder>");
	}

	// Add the condense instructions as the final user message
	FAgentZetMessage InstructionsMsg;
	InstructionsMsg.MessageId = FGuid::NewGuid();
	InstructionsMsg.Role = EAgentZetMessageRole::User;
	InstructionsMsg.Content = InstructionsContent;
	InstructionsMsg.Timestamp = FDateTime::UtcNow();
	PreparedMessages.Add(InstructionsMsg);

	// Capture options needed for ApplyCondensation after the async response
	const FString CapturedCommandBlocks = CommandBlocks;
	const FString CapturedFoldedCodeContext = Options.FoldedCodeContext;
	const bool bCapturedIsAutomaticTrigger = Options.bIsAutomaticTrigger;
	const FString CapturedEnvironmentDetails = Options.EnvironmentDetails;

	// Bind delegates for the summarization response
	TSharedPtr<FString> SummaryAccumulator = MakeShared<FString>();
	TSharedPtr<bool> bCompleted = MakeShared<bool>(false);

	StreamingHandle = LLMClient->OnStreamingText().AddLambda(
		[SummaryAccumulator](const FGuid& MsgId, const FString& DeltaText)
		{
			*SummaryAccumulator += DeltaText;
		});

	RequestCompletedHandle = LLMClient->OnRequestCompleted().AddLambda(
		[this, SummaryAccumulator, bCompleted,
		 CapturedCommandBlocks, CapturedFoldedCodeContext,
		 bCapturedIsAutomaticTrigger, CapturedEnvironmentDetails]
		(bool bSuccess)
		{
			if (*bCompleted) return;
			*bCompleted = true;

			if (LLMClient.IsValid())
			{
				LLMClient->OnStreamingText().Remove(StreamingHandle);
				LLMClient->OnMessageComplete().Remove(MessageCompleteHandle);
				LLMClient->OnRequestCompleted().Remove(RequestCompletedHandle);
			}

			bIsCondensing = false;

			FAgentZetCondenseResult Result;

			if (!bSuccess || SummaryAccumulator->IsEmpty())
			{
				Result.bSuccess = false;
				Result.ErrorMessage = TEXT("Condensation API call failed or returned empty summary.");
				UE_LOG(LogAgentZet, Error, TEXT("ContextCondenser: %s"), *Result.ErrorMessage);

				auto LocalCallback = MoveTemp(PendingCallback);
				PendingCallback = nullptr;
				if (LocalCallback) { LocalCallback(Result); }
				return;
			}

			FString Summary = SummaryAccumulator->TrimStartAndEnd();

			if (Summary.IsEmpty())
			{
				Result.bSuccess = false;
				Result.ErrorMessage = TEXT("Condensation returned empty summary.");
				UE_LOG(LogAgentZet, Error, TEXT("ContextCondenser: %s"), *Result.ErrorMessage);

				auto LocalCallback = MoveTemp(PendingCallback);
				PendingCallback = nullptr;
				if (LocalCallback) { LocalCallback(Result); }
				return;
			}

			Result.bSuccess = true;
			Result.Summary = Summary;
			Result.CondenseId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);

			ApplyCondensation(Summary, Result,
				CapturedCommandBlocks, CapturedFoldedCodeContext,
				bCapturedIsAutomaticTrigger, CapturedEnvironmentDetails);

			if (ConversationManager.IsValid())
			{
				TArray<FAgentZetMessage> EffectiveAfter = ConversationManager->GetEffectiveHistory();
				Result.NewContextTokens = FAgentZetTokenCounter::EstimateTokens(EffectiveAfter);
			}

			UE_LOG(LogAgentZet, Log,
				TEXT("ContextCondenser: Condensed %d messages. Summary length: %d chars. New context: ~%d tokens. CondenseId: %s"),
				ConversationManager->GetMessageCount(), Summary.Len(), Result.NewContextTokens, *Result.CondenseId);

			auto LocalCallback = MoveTemp(PendingCallback);
			PendingCallback = nullptr;
			if (LocalCallback) { LocalCallback(Result); }
		});

	// Send the summarization request — no tool schemas (text-only)
	LLMClient->SendMessage(PreparedMessages, SummaryPrompt, TArray<TSharedPtr<FJsonObject>>());
}

// ============================================================================
// Convert tool blocks to text
// ============================================================================

TArray<FAgentZetMessage> FAgentZetContextCondenser::ConvertToolBlocksToText(const TArray<FAgentZetMessage>& Messages)
{
	TArray<FAgentZetMessage> Result;

	for (const FAgentZetMessage& Msg : Messages)
	{
		FAgentZetMessage ConvertedMsg = Msg;

		if (Msg.Role == EAgentZetMessageRole::Assistant && !Msg.ContentBlocksJson.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> ContentBlocks;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Msg.ContentBlocksJson);
			if (FJsonSerializer::Deserialize(Reader, ContentBlocks))
			{
				FString TextContent;
				for (const TSharedPtr<FJsonValue>& Block : ContentBlocks)
				{
					const TSharedPtr<FJsonObject>* BlockObj = nullptr;
					if (!Block->TryGetObject(BlockObj)) continue;

					FString BlockType;
					(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);

					if (BlockType == TEXT("text"))
					{
						FString Text;
						(*BlockObj)->TryGetStringField(TEXT("text"), Text);
						TextContent += Text;
					}
					else if (BlockType == TEXT("tool_use"))
					{
						FString ToolName;
						(*BlockObj)->TryGetStringField(TEXT("name"), ToolName);

						const TSharedPtr<FJsonObject>* InputObj = nullptr;
						FString InputStr;
						if ((*BlockObj)->TryGetObjectField(TEXT("input"), InputObj))
						{
							TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&InputStr);
							FJsonSerializer::Serialize((*InputObj).ToSharedRef(), Writer);
						}

						TextContent += FString::Printf(TEXT("\n[Tool Use: %s]\n%s\n"), *ToolName, *InputStr);
					}
				}
				ConvertedMsg.Content = TextContent;
				ConvertedMsg.ContentBlocksJson.Empty();
			}
		}
		else if (Msg.Role == EAgentZetMessageRole::ToolResult)
		{
			FString Prefix = TEXT("[Tool Result]\n");
			ConvertedMsg.Content = Prefix + Msg.Content;
		}

		Result.Add(ConvertedMsg);
	}

	return Result;
}

// ============================================================================
// Inject synthetic tool results
// ============================================================================

void FAgentZetContextCondenser::InjectSyntheticToolResults(TArray<FAgentZetMessage>& Messages)
{
	TSet<FString> ToolCallIds;
	TSet<FString> ToolResultIds;

	for (const FAgentZetMessage& Msg : Messages)
	{
		if (Msg.Role == EAgentZetMessageRole::Assistant && !Msg.ContentBlocksJson.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> ContentBlocks;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Msg.ContentBlocksJson);
			if (FJsonSerializer::Deserialize(Reader, ContentBlocks))
			{
				for (const TSharedPtr<FJsonValue>& Block : ContentBlocks)
				{
					const TSharedPtr<FJsonObject>* BlockObj = nullptr;
					if (!Block->TryGetObject(BlockObj)) continue;

					FString BlockType;
					(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);
					if (BlockType == TEXT("tool_use"))
					{
						FString Id;
						(*BlockObj)->TryGetStringField(TEXT("id"), Id);
						if (!Id.IsEmpty()) ToolCallIds.Add(Id);
					}
				}
			}
		}
		else if (Msg.Role == EAgentZetMessageRole::ToolResult)
		{
			if (!Msg.ToolUseId.IsEmpty()) ToolResultIds.Add(Msg.ToolUseId);
		}
	}

	TArray<FString> OrphanIds;
	for (const FString& Id : ToolCallIds)
	{
		if (!ToolResultIds.Contains(Id))
		{
			OrphanIds.Add(Id);
		}
	}

	if (OrphanIds.Num() == 0) return;

	for (const FString& OrphanId : OrphanIds)
	{
		FAgentZetMessage SyntheticResult;
		SyntheticResult.MessageId = FGuid::NewGuid();
		SyntheticResult.Role = EAgentZetMessageRole::ToolResult;
		SyntheticResult.ToolUseId = OrphanId;
		SyntheticResult.Content = TEXT("Context condensation triggered. Tool execution deferred.");
		SyntheticResult.Timestamp = FDateTime::UtcNow();
		Messages.Add(SyntheticResult);

		UE_LOG(LogAgentZet, Log,
			TEXT("ContextCondenser: Injected synthetic tool_result for orphan tool_use id=%s"), *OrphanId);
	}
}

// ============================================================================
// Apply condensation (v3.1 — structured summary with system-reminder blocks)
// ============================================================================

void FAgentZetContextCondenser::ApplyCondensation(
	const FString& Summary, const FAgentZetCondenseResult& Result,
	const FString& CommandBlocks, const FString& FoldedCodeContext,
	bool bIsAutomaticTrigger, const FString& EnvironmentDetails)
{
	if (!ConversationManager.IsValid()) return;

	TArray<FAgentZetMessage>& History = const_cast<TArray<FAgentZetMessage>&>(ConversationManager->GetHistory());

	// Tag ALL existing messages with CondenseParent (fresh start model)
	for (FAgentZetMessage& Msg : History)
	{
		if (Msg.CondenseParent.IsEmpty() && !Msg.bIsSummary)
		{
			Msg.CondenseParent = Result.CondenseId;
		}
	}

	// Build structured summary content (mirrors Zoo-Code's multi-block summary)
	FString SummaryContent;
	SummaryContent += TEXT("## Conversation Summary\n");
	SummaryContent += Summary;

	// Add preserved <command> blocks as system-reminder
	if (!CommandBlocks.IsEmpty())
	{
		SummaryContent += TEXT("\n\n<system-reminder>\n## Active Workflows\n");
		SummaryContent += TEXT("The following directives must be maintained across all future condensings:\n");
		SummaryContent += CommandBlocks;
		SummaryContent += TEXT("\n</system-reminder>");
	}

	// Add folded file context as system-reminder
	if (!FoldedCodeContext.IsEmpty())
	{
		SummaryContent += TEXT("\n\n<system-reminder>\n## File Context (from task)\n");
		SummaryContent += FoldedCodeContext;
		SummaryContent += TEXT("\n</system-reminder>");
	}

	// Add environment details for auto-condense only
	if (bIsAutomaticTrigger && !EnvironmentDetails.IsEmpty())
	{
		SummaryContent += TEXT("\n\n");
		SummaryContent += EnvironmentDetails;
	}

	// Insert the summary message at the end
	FAgentZetMessage SummaryMsg;
	SummaryMsg.MessageId = FGuid::NewGuid();
	SummaryMsg.Role = EAgentZetMessageRole::User;
	SummaryMsg.Content = SummaryContent;
	SummaryMsg.Timestamp = FDateTime::UtcNow();
	SummaryMsg.bIsSummary = true;
	SummaryMsg.CondenseId = Result.CondenseId;

	History.Add(SummaryMsg);

	UE_LOG(LogAgentZet, Log,
		TEXT("ContextCondenser: Summary message created with %d chars content. CondenseId=%s"),
		SummaryContent.Len(), *Result.CondenseId);
}
