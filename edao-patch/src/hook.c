#include <stdint.h>
#include <string.h>
#include "hook.h"


#define DECL_INLINE_ASM_ARM __attribute__((naked, target("arm")))
#define DECL_INLINE_ASM_THUMB __attribute__((naked, target("thumb")))

#define DECL_FUNCTION_ARM __attribute__((target("arm")))
#define DECL_FUNCTION_THUMB __attribute__((target("thumb")))
#define DECL_FUNCTION_NOINLINE __attribute__ ((noinline))

#define ASM_PUSHAD "stmfd sp!, {r0-r12}	\n\t"
#define ASM_POPAD "ldmfd sp!, {r0-r12}	\n\t"
#define ASM_PUSHFD(r) \
	"mrs " #r ", cpsr	\n\t" \
	"str " #r ", [sp, #-4]!		\n\t"
#define ASM_POPFD(r) \
	"ldr " #r ", [sp], #4		\n\t" \
	"msr cpsr," #r "			\n\t"


///////////////////////////////////////////////////////////

TL_CONTEXT g_tl_context_scena;
TL_CONTEXT g_tl_context_sys;

///////////////////////////////////////////////////////////

typedef struct hook_context_s
{
	void* new_func;
	void* old_func;
} hook_context;



hook_context* p_ctx_scp_process_scena = (hook_context*)0x8127C9A8;
hook_context* p_ctx_scp_process_story = (hook_context*)0x8127C9B0;
hook_context* p_ctx_scp_draw_item1 = (hook_context*)0x8127C9B8;
hook_context* p_ctx_scp_draw_item2 = (hook_context*)0x8127C9C0;


typedef char* (* pfunc_scp_process_scena) (void* this, char* opcode, char* name, uint32_t uk);
pfunc_scp_process_scena old_scp_process_scena = NULL;

typedef char* (* pfunc_scp_process_story) (void* this, char* opcode, char* str, uint32_t uk);
pfunc_scp_process_story old_scp_process_story = NULL;


typedef uint32_t (* pfunc_draw_item1)(void* this, uint32_t uk1, uint32_t uk2, char* str, uint32_t uk3);
pfunc_draw_item1 old_draw_item1 = NULL;

typedef uint32_t (* pfunc_draw_item2)(void* this, uint32_t uk1, uint32_t uk2, char* str, uint32_t uk3, uint32_t uk4);
pfunc_draw_item2 old_draw_item2 = NULL;


///////////////////////////////////////////////////////////
typedef struct SUBSTR_ITEM_S
{
	char* sub_str;
	uint32_t sub_len;
} SUBSTR_ITEM;


int has_sharp(const char* string, uint32_t len)
{
	if (!string)
	{
		return 0;
	}

	for (uint32_t i = 0; i != len; ++i)
	{
		if (string[i] == '#')
		{
			return 1;
		}
	}

	return 0;
}


SUBSTR_ITEM* find_item(const char* parent_str, uint32_t position,
					   SUBSTR_ITEM items[], uint32_t item_count)
{
	for (uint32_t i = 0; i != item_count; ++i)
	{
		uint32_t cur_pointer = (uint32_t)parent_str + position;
		if (cur_pointer >= (uint32_t)items[i].sub_str &&
			cur_pointer < (uint32_t)items[i].sub_str + items[i].sub_len)
		{
			return &items[i];
		}
	}
	return NULL;
}

void split_string(const char* string, uint32_t str_len, 
				  SUBSTR_ITEM items[], uint32_t* item_count)
{
	if (!string || !item_count)
	{
		return;
	}

	//split string by non visual char
	uint32_t count = *item_count;
	uint32_t n = 0;
	uint32_t cur_size = 0;
	items[n].sub_str = (char*)string;
	for (uint32_t i = 0; i != str_len; ++i)
	{
		uint8_t ch = (uint8_t)string[i];
		if (ch >= 0x20 && ch != '#')
		{
			cur_size++;
			continue;
		}
		else
		{
			if (cur_size == 0)
			{
				if (ch != '#')
				{
					items[n].sub_str++;	
				}
				else
				{
					cur_size++;
				}
				continue;
			}
		}

		items[n].sub_len = cur_size;
		n++;
		cur_size = 0;

		if (n >= count)
		{
			break;
		}

		uint32_t j = i;
		for (; j != str_len; ++j)
		{
			if ((uint8_t)string[j] == '#')
			{
				items[n].sub_str = (char*)&string[j];
				j++;
				cur_size++;
				break;
			}

			if ((uint8_t)string[j] < 0x20)
			{
				continue;
			}

			items[n].sub_str = (char*)&string[j];
			break;
		}
		i = j - 1;
	}

	if (cur_size != 0)
	{
		items[n].sub_len = cur_size;
		n++;
	}

	*item_count = n;
	for (uint32_t k = 0; k != n; ++k)
	{
		char* str = items[k].sub_str;
		uint32_t len = items[k].sub_len;

		if (!has_sharp(str, len))
		{
			continue;
		}

		//if string contains "#", we skip over the opcodes.
		uint32_t m = 0;
		for (; m != len; ++m)
		{
			if (!is_acsii_char(str[m]))
			{
				break;
			}
		}

		items[k].sub_str = str + m;
		items[k].sub_len = len - m;
	}
}


void translate_string(TL_CONTEXT* tl_ctx, const char* old_str, uint32_t old_len,
					  char* new_str, uint32_t* new_len,
					  SUBSTR_ITEM items[], uint32_t item_count)
{
	if (!old_str || !new_str)
	{
		return;
	}

	uint32_t n = 0;
	uint32_t buffer_len = *new_len;
	for (uint32_t i = 0; i != old_len; ++i)
	{
		SUBSTR_ITEM* item = find_item(old_str, i, items, item_count);
		if (item == NULL)
		{
			new_str[n++] = old_str[i];
			continue;
		}

		//assert(item->sub_str == old_str + i);
		uint32_t translate_len = *new_len - n - 1;  //will be translated length after call
		int is_tranlated = tl_translate(tl_ctx,
										item->sub_str, item->sub_len, 
										&new_str[n], &translate_len);

		if (is_tranlated)
		{
			n += translate_len;
		}
		else
		{
			strncpy(&new_str[n], item->sub_str, item->sub_len);
			n += item->sub_len;
		}

		if (n >= buffer_len)
		{
			break;  //make sure we are safe :)
		}

		i += (item->sub_len - 1);
	}

	*new_len = n;
}


void translate_name(char* name)
{
	if (!name || !*name)
	{
		return;
	}

	uint32_t name_len = strlen(name);
	int tranlated = tl_translate(&g_tl_context_scena,
				 name, name_len, 
				 name, &name_len);
	if (tranlated)
	{
		name[name_len] = 0;
	}
}


/*
From IDA:

switch ( code )
{
  case 0:
  	...
    goto PROC_FINISH;
  case 1:
  case 0xA:
  	...
    ++cur_pointer;
    next_buffer = buffer + 1;
    goto LABEL_88;
  case 2:
  case 5:
    v53 = char_rect;
    ++cur_pointer;
    next_buffer = buffer + 1;
    goto LABEL_88;
  case 3:
  case 4:
  	...
    ++cur_pointer;
    next_buffer = buffer + 1;
    goto LABEL_88;
  case 6:
    ++cur_pointer;
    ...
    next_buffer = v60 + 1;

    goto LABEL_88;
  case 7:
    cur_pointer += 2;
    v58 = *text___++;
    v59 = buffer + 1;
    *buffer = v58;
    buffer = v59;
    next_buffer += 2;
    v53 = char_rect;
    goto LABEL_88;
  case 0x1F:
  	...
    cur_pointer += 3;
    next_buffer += 3;
    goto PROC_FINISH;
  default:
    ++cur_pointer;
    next_buffer = buffer + 1;
}
*/


uint32_t get_code_len(char code)
{
	uint32_t len = 0;

	if (code >= 0x20)
	{
		return 1;
	}

	switch (code)
	{
	  case 0:	//should never be here!
	  	len = 0;
	  	break;
	  case 1:
	  case 0xA:
		len = 1;
		break;
	  case 2:
	  case 5:
		len = 1;
		break;
	  case 3:
	  case 4:
		len = 1;
		break;
	  case 6:
		len = 1;
		break;
	  case 7:
		len = 2;
		break;
	  case 0x1F:
		len = 3;
		break;
	  default:
		len = 1;
		break;
	}
	return len;
}

uint32_t parse_opcode_len(char* opcode)
{
	if (!opcode || !*opcode)
	{
		return 0;
	}

	uint32_t opcode_len = 0;
	uint32_t str_len = 0;
	uint32_t tail_len = 0;
	char tail = 0;
	char* code = opcode;

	do
	{
		if(*code<0x20)
		{
			tail = *code;
			tail_len = get_code_len(tail);
			code += tail_len;
			opcode_len+=tail_len;
		}
		else
		{
			str_len = strlen(code);
			if((*code+str_len-1)<0x20)
			{
				opcode_len+=str_len-1;
				code+=str_len-1;
			}
			else
			{
				opcode_len += str_len;
				code += str_len;
			}
			
		}
	} while(*code != 0);

	return opcode_len;
}


#define SUBSTR_ITEM_MAX 32
#define DEFAULT_TRANSLATE_BUFF_LEN (4096 * 5)
static char tl_buffer[DEFAULT_TRANSLATE_BUFF_LEN] = {0};


DECL_FUNCTION_THUMB
char* new_scp_process_scena(void* this, char* opcode, char* name, uint32_t uk)
{
	char* this_opcode = NULL;
	char* next_opcode = NULL;
	uint32_t opcode_len = 0;
	SUBSTR_ITEM str_items[SUBSTR_ITEM_MAX] = {0};

	int is_translated = 0;

	do
	{
		if (!opcode || !*opcode)
		{		
			this_opcode = opcode;
			break;
		}


		translate_name(name);

	 	opcode_len = parse_opcode_len(opcode);
		uint32_t item_count = SUBSTR_ITEM_MAX;  //will be real count after call
		split_string(opcode, opcode_len, str_items, &item_count);

		//dump_mem("Before:", (uint8_t*)opcode, opcode_len);

		uint32_t translate_len = DEFAULT_TRANSLATE_BUFF_LEN;  //will be real tranlated len after call
		translate_string(&g_tl_context_scena, opcode, opcode_len,
						 tl_buffer, &translate_len, 
						 str_items, item_count);
		tl_buffer[translate_len] = 0;

		this_opcode = tl_buffer;
	 	is_translated = 1;

		//dump_mem("After:", (uint8_t*)tl_buffer, translate_len);
	} while(0);


	old_scp_process_scena = (pfunc_scp_process_scena)ADDR_THUMB(p_ctx_scp_process_scena->old_func);
	next_opcode = old_scp_process_scena(this, this_opcode, name, uk);

	if (is_translated && next_opcode != NULL)
	{
		next_opcode = opcode + opcode_len + 1;
	}

	return next_opcode;
}


static char tl_buffer_story[DEFAULT_TRANSLATE_BUFF_LEN] = {0};
DECL_FUNCTION_THUMB
char* new_scp_process_story(void* this, char* opcode, char* str, uint32_t uk)
{
    char* this_opcode = NULL;
    char* next_opcode = NULL;
    uint32_t opcode_len = 0;
    SUBSTR_ITEM str_items[SUBSTR_ITEM_MAX] = {0};

    int is_translated = 0;

    do
    {
        if (!opcode || !*opcode)
        {
            this_opcode = opcode;
            break;
        }


        opcode_len = parse_opcode_len(opcode);
        uint32_t item_count = SUBSTR_ITEM_MAX;  //will be real count after call
        split_string(opcode, opcode_len, str_items, &item_count);

        //dump_mem("Before:", (uint8_t*)opcode, opcode_len);

        uint32_t translate_len = DEFAULT_TRANSLATE_BUFF_LEN;  //will be real tranlated len after call
        translate_string(&g_tl_context_sys, opcode, opcode_len,
                         tl_buffer_story, &translate_len,
                         str_items, item_count);
        tl_buffer_story[translate_len] = 0;

        this_opcode = tl_buffer_story;
        is_translated = 1;

        //dump_mem("After:", (uint8_t*)tl_buffer, translate_len);
    } while(0);


    old_scp_process_story = (pfunc_scp_process_story)ADDR_THUMB(p_ctx_scp_process_story->old_func);
    next_opcode = old_scp_process_story(this, this_opcode, str, uk);

    if (is_translated && next_opcode != NULL)
    {
        next_opcode = opcode + opcode_len + 1;
    }

    return next_opcode;
}

DECL_FUNCTION_THUMB
uint32_t new_draw_item1(void* this, uint32_t uk1, uint32_t uk2, char* str, uint32_t uk3)
{
    char* new_str = NULL;
    char cn_buffer[DEFAULT_TRANSLATE_BUFF_LEN] = {0};

    old_draw_item1 = (pfunc_draw_item1)ADDR_THUMB(p_ctx_scp_draw_item1->old_func);

    //dump_mem("draw1:", str, strlen(str));

    new_str = str;

//    if (str && *str)
//    {
//        int old_len = strlen(str);
//        uint32_t translate_len = DEFAULT_TRANSLATE_BUFF_LEN;
//        int is_tranlated = tl_translate(&g_tl_context_sys,
//                                        str, old_len,
//                                        cn_buffer, &translate_len);
//        if (is_tranlated)
//        {
//            new_str = cn_buffer;
//        }
//        else
//        {
//            new_str = "draw1 miss";
//        }
//    }

    return old_draw_item1(this, uk1, uk2, new_str, uk3);
}


DECL_FUNCTION_NOINLINE
uint32_t test_arg(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5)
{
    uint32_t ret = arg1 + arg2 + arg3 + arg4 + arg5;
    uint32_t count = 0x1000000;
    while (count--)
    {
        ret += count;
    }
    return ret + 1;
}

uint32_t bkdr_hash(const uint8_t* string, uint32_t len);

DECL_FUNCTION_NOINLINE
int test_translate(TL_CONTEXT* ctx, const char* jp_str, uint32_t jp_len,
                 char* cn_str, uint32_t* cn_len)
{
    uint32_t hash = bkdr_hash((uint8_t*)jp_str, jp_len);
    memcpy(cn_str, jp_str, jp_len);
    return hash;
}



DECL_FUNCTION_THUMB
uint32_t new_draw_item2(void* this, uint32_t uk1, uint32_t uk2, char* str, uint32_t uk3, uint32_t uk4)
{
    //char* new_str = NULL;
    static char cn_buffer[DEFAULT_TRANSLATE_BUFF_LEN] = {0};

    old_draw_item2 = (pfunc_draw_item2)ADDR_THUMB(p_ctx_scp_draw_item2->old_func);

    memcpy(cn_buffer, str, strlen(str));

    return old_draw_item2(this, uk1, uk2, str, uk3, uk4);

//    new_str = str;
//
//    if (str && *str)
//    {
//        memset(cn_buffer, 0, DEFAULT_TRANSLATE_BUFF_LEN);
//        int old_len = strlen(str);
//        uint32_t translate_len = DEFAULT_TRANSLATE_BUFF_LEN - 1;
////        int is_tranlated = tl_translate(&g_tl_context_sys,
////                                        str, old_len,
////                                        cn_buffer, &translate_len);
//
//        int is_tranlated = test_translate(&g_tl_context_sys,
//                                        str, old_len,
//                                        cn_buffer, &translate_len);
//
////        int is_tranlated = test_arg((uint32_t)&g_tl_context_sys,
////                                    (uint32_t)str, old_len,
////                                    (uint32_t)cn_buffer, (uint32_t)&translate_len);
////
//        if (is_tranlated)
//        //if (1)
//        {
////            dump_mem("draw2 old:", new_str, strlen(new_str) + 0x10);
////            new_str = cn_buffer;
////            dump_mem("draw2 new:", new_str, strlen(new_str) + 0x10);
//
//            //new_str = "\x96\xC8\x97\x65\x84\xB6\x96\xC8\x97\x65\x84\xB6\x00";
//            new_str = "HIJKLMN";
//        }
//        else
//        {
//            new_str = "draw2 miss";
//        }
//    }

    //return old_draw_item2(this, uk1, uk2, new_str, uk3, uk4);

}


///////////////////////////////////////////////////////////

int init_hooks()
{

	p_ctx_scp_process_scena->new_func = (void*)ADDR_THUMB(new_scp_process_scena);
    p_ctx_scp_process_story->new_func = (void*)ADDR_THUMB(new_scp_process_story);
    p_ctx_scp_draw_item1->new_func = (void*)ADDR_THUMB(new_draw_item1);
    p_ctx_scp_draw_item2->new_func = (void*)ADDR_THUMB(new_draw_item2);

	return 0;
}
